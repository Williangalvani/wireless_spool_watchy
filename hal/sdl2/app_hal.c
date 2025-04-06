#include <unistd.h>
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <stdbool.h>
#include <stdlib.h>

// Function prototypes
void hal_setup(void);
void hal_loop(void);
void hal_cleanup(void);

// Display dimensions
#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
#define SDL_HOR_RES DISPLAY_WIDTH
#define SDL_VER_RES DISPLAY_HEIGHT

static SDL_Window * window;
static SDL_Renderer * renderer;
static SDL_Texture * texture;
static uint32_t * pixel_buffer;

static lv_disp_drv_t disp_drv;  // LVGL v8 display driver
static lv_indev_drv_t indev_drv_mouse;  // Mouse input driver
static lv_indev_drv_t indev_drv_keyboard;  // Keyboard input driver
static lv_indev_t * mouse_indev;
static lv_indev_t * keyboard_indev;

// Temporary buffer for error diffusion dithering (holds grayscale values)
static uint8_t *dither_buffer = NULL;

// Function to perform Floyd-Steinberg dithering
static void dither_image(uint8_t *pixels, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Get current pixel
            uint8_t oldPixel = pixels[y * width + x];
            
            // Apply threshold to decide black or white
            uint8_t newPixel = (oldPixel < 128) ? 0 : 255;
            
            // Calculate quantization error
            int error = oldPixel - newPixel;
            
            // Store the new pixel
            pixels[y * width + x] = newPixel;
            
            // Distribute error to neighboring pixels
            if (x + 1 < width)
                pixels[y * width + x + 1] = (uint8_t)((pixels[y * width + x + 1] + error * 7 / 16) > 255 ? 255 : ((pixels[y * width + x + 1] + error * 7 / 16) < 0 ? 0 : (pixels[y * width + x + 1] + error * 7 / 16)));
            
            if (y + 1 < height) {
                if (x > 0)
                    pixels[(y + 1) * width + x - 1] = (uint8_t)((pixels[(y + 1) * width + x - 1] + error * 3 / 16) > 255 ? 255 : ((pixels[(y + 1) * width + x - 1] + error * 3 / 16) < 0 ? 0 : (pixels[(y + 1) * width + x - 1] + error * 3 / 16)));
                    
                pixels[(y + 1) * width + x] = (uint8_t)((pixels[(y + 1) * width + x] + error * 5 / 16) > 255 ? 255 : ((pixels[(y + 1) * width + x] + error * 5 / 16) < 0 ? 0 : (pixels[(y + 1) * width + x] + error * 5 / 16)));
                
                if (x + 1 < width)
                    pixels[(y + 1) * width + x + 1] = (uint8_t)((pixels[(y + 1) * width + x + 1] + error * 1 / 16) > 255 ? 255 : ((pixels[(y + 1) * width + x + 1] + error * 1 / 16) < 0 ? 0 : (pixels[(y + 1) * width + x + 1] + error * 1 / 16)));
            }
        }
    }
}

static void flush_cb(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * px_map)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    // Check for invalid dimensions to prevent segfaults
    if (w <= 0 || h <= 0 || w > SDL_HOR_RES || h > SDL_VER_RES) {
        printf("Warning: Invalid dimensions in flush_cb: %dx%d\n", w, h);
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    printf("Flushing area: x1=%d, y1=%d, x2=%d, y2=%d, size=%dx%d\n", 
           area->x1, area->y1, area->x2, area->y2, w, h);
    
    // Check if pixel_buffer is valid before writing to it
    if (pixel_buffer == NULL) {
        printf("Error: pixel_buffer is NULL in flush_cb\n");
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    // Simplified version - direct copy without dithering
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int px = area->x1 + x;
            int py = area->y1 + y;
            
            // Bounds check
            if (px >= 0 && px < SDL_HOR_RES && py >= 0 && py < SDL_VER_RES) {
                uint32_t pos = py * SDL_HOR_RES + px;
                
                // Get the color from LVGL
                lv_color_t color = px_map[y * w + x];
                
                // Convert LVGL color to ARGB for SDL
                // For e-paper emulation, use grayscale instead of pure black/white
                uint8_t brightness = lv_color_brightness(color);
                // Create a grayscale color with alpha=255
                uint32_t sdl_color = 0xFF000000 | (brightness << 16) | (brightness << 8) | brightness;
                
                pixel_buffer[pos] = sdl_color;
            }
        }
    }
    
    // Update SDL texture and render
    if (SDL_UpdateTexture(texture, NULL, pixel_buffer, SDL_HOR_RES * 4) != 0) {
        printf("Error: Failed to update texture: %s\n", SDL_GetError());
        lv_disp_flush_ready(disp_drv);
        return;
    }
    
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    
    // Inform LVGL that the flushing is done
    lv_disp_flush_ready(disp_drv);
}

static void mouse_read_cb(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static int16_t last_x = 0;
    static int16_t last_y = 0;

    // Get mouse position and button state
    int x, y;
    int state = SDL_GetMouseState(&x, &y);

    data->point.x = x;
    data->point.y = y;
    data->state = state & SDL_BUTTON(1) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    last_x = x;
    last_y = y;
}

static void keyboard_read_cb(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static SDL_Event e;
    
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;
    
    // Poll events until no more are in the queue
    while(SDL_PollEvent(&e)) {
        if(e.type == SDL_KEYDOWN) {
            switch(e.key.keysym.sym) {
                case SDLK_UP:
                    data->key = LV_KEY_UP;
                    break;
                case SDLK_DOWN:
                    data->key = LV_KEY_DOWN;
                    break;
                case SDLK_LEFT:
                    data->key = LV_KEY_LEFT;
                    break;
                case SDLK_RIGHT:
                    data->key = LV_KEY_RIGHT;
                    break;
                case SDLK_ESCAPE:
                    data->key = LV_KEY_ESC;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    data->key = LV_KEY_ENTER;
                    break;
                default:
                    data->key = 0;
                    break;
            }
            
            if(data->key != 0) {
                data->state = LV_INDEV_STATE_PRESSED;
                return;
            }
        }
        
        if(e.type == SDL_QUIT) {
            // Handle window close
            exit(0);
        }
    }
}

void hal_setup(void)
{
    // Workaround for sdl2 `-m32` crash
    // https://bugs.launchpad.net/ubuntu/+source/libsdl2/+bug/1775067/comments/7
    #ifndef WIN32
        setenv("DBUS_FATAL_WARNINGS", "0", 1);
    #endif

    printf("Starting HAL setup...\n");

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL init error: %s\n", SDL_GetError());
        return;
    }
    
    printf("SDL initialized\n");
    
    // Set window title to indicate e-paper simulation
    window = SDL_CreateWindow("LVGL E-Paper Simulator",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SDL_HOR_RES, SDL_VER_RES,  // Use actual size for window
                              SDL_WINDOW_SHOWN);
    
    if (window == NULL) {
        printf("Window creation error: %s\n", SDL_GetError());
        SDL_Quit();
        return;
    }
    
    printf("Window created\n");
    
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("Renderer creation error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    
    printf("Renderer created\n");
    
    // Enable alpha blending
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STATIC, SDL_HOR_RES, SDL_VER_RES);
    if (texture == NULL) {
        printf("Texture creation error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    
    printf("Texture created\n");
    
    // Calculate buffer size and print it
    size_t buffer_size = SDL_HOR_RES * SDL_VER_RES * 4;
    printf("Allocating pixel buffer of size: %zu bytes\n", buffer_size);
    
    // Create pixel buffer
    pixel_buffer = (uint32_t*)malloc(buffer_size);
    if (pixel_buffer == NULL) {
        printf("Failed to allocate pixel buffer\n");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    
    printf("Pixel buffer allocated\n");
    
    // Set background to white like an e-paper display
    memset(pixel_buffer, 0xFF, buffer_size);
    
    // Update SDL texture and render the white background
    if (SDL_UpdateTexture(texture, NULL, pixel_buffer, SDL_HOR_RES * 4) != 0) {
        printf("Failed to update texture: %s\n", SDL_GetError());
    }
    
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    printf("Initializing LVGL display\n");
    
    // Initialize LVGL display
    static lv_disp_draw_buf_t draw_buf;
    // Use a larger buffer - 1/4 of the screen instead of 1/10
    static lv_color_t buf[SDL_HOR_RES * SDL_VER_RES / 4];
    
    printf("LVGL buffer size: %zu bytes\n", sizeof(buf));
    
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SDL_HOR_RES * SDL_VER_RES / 4);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SDL_HOR_RES;
    disp_drv.ver_res = SDL_VER_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    printf("LVGL display initialized\n");
    
    // Initialize mouse input
    lv_indev_drv_init(&indev_drv_mouse);
    indev_drv_mouse.type = LV_INDEV_TYPE_POINTER;
    indev_drv_mouse.read_cb = mouse_read_cb;
    mouse_indev = lv_indev_drv_register(&indev_drv_mouse);
    
    // Initialize keyboard input
    lv_indev_drv_init(&indev_drv_keyboard);
    indev_drv_keyboard.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv_keyboard.read_cb = keyboard_read_cb;
    keyboard_indev = lv_indev_drv_register(&indev_drv_keyboard);
    
    printf("Input devices initialized\n");
    printf("HAL setup complete\n");
}

void hal_loop(void)
{
    // Handle SDL events
    SDL_Event event;
    
    // Process events in the queue but don't block
    while(SDL_PollEvent(&event)) {
        if(event.type == SDL_QUIT) {
            printf("SDL_QUIT event received\n");
            hal_cleanup();
            SDL_Quit();
            exit(0);
        }
    }
    
    // Update LVGL time
    uint32_t current = SDL_GetTicks();
    static uint32_t last_tick = 0;
    
    if(last_tick == 0) {
        last_tick = current;
    }
    
    uint32_t elapsed = current - last_tick;
    
    if(elapsed > 0) {
        lv_tick_inc(elapsed);
        last_tick = current;
    }
    
    // Call LVGL task handler
    lv_task_handler();
    
    // Add small delay to prevent 100% CPU usage
    SDL_Delay(10);  // Increased from 5ms to 10ms
}

void hal_cleanup(void)
{
    printf("Cleaning up resources...\n");
    
    // Free dither buffer
    if (dither_buffer != NULL) {
        free(dither_buffer);
        dither_buffer = NULL;
    }
    
    // Free pixel buffer
    if (pixel_buffer != NULL) {
        free(pixel_buffer);
        pixel_buffer = NULL;
    }
    
    // Clean up SDL resources - check each pointer before destroying
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    
    // Finally quit SDL subsystems
    SDL_Quit();
    
    printf("Cleanup complete\n");
}
