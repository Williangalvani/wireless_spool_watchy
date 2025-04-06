/**
  ******************************************************************************
  * @file    main.cpp
  * @author  LVGL for Watchy e-paper display
  * @brief   Main program for Watchy with LVGL
  ******************************************************************************
*/

#include <lvgl.h>
#include <stdio.h>
#include <unistd.h>
#include <exception>

// Include different headers based on environment
#if defined(ARDUINO)
  #include <Arduino.h>
  #include <SPI.h>
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Fonts/FreeMonoBold9pt7b.h>
  #include "../hal/esp32/displays/LGFX_WATCHY_EPAPER.hpp"
#else
  // SDL emulator environment
  #include "../hal/sdl2/app_hal.h"
  
  // Define the same dimensions as the SDL HAL to prevent segfault
  #define DISPLAY_WIDTH 480
  #define DISPLAY_HEIGHT 320
  #define DISPLAY_BITS_PER_PIXEL 1
#endif

// Define a default font explicitly for LVGL to use
LV_FONT_DECLARE(lv_font_montserrat_14);

// Buffer size calculation for the display
const uint32_t lvScreenWidth = DISPLAY_WIDTH;
const uint32_t lvScreenHeight = DISPLAY_HEIGHT;
const uint32_t lvBufferSize = (lvScreenWidth * lvScreenHeight / 10); // 1/10 of screen for drawing buffer

// Use same buffer settings for both Arduino and emulator
static lv_color_t lvBuffer[lvBufferSize]; // Buffer for LVGL - using color_t for standard LVGL format

#if defined(ARDUINO)
// Define the display instance - simplified for monochrome display
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(
    DISPLAY_CS,
    DISPLAY_DC,
    DISPLAY_RESET,
    DISPLAY_BUSY
  )
);

// LVGL display driver related objects
static lv_disp_draw_buf_t draw_buf; // Buffer for drawing
static lv_disp_drv_t disp_drv;      // Display driver
static lv_indev_drv_t indev_drv;    // Input device driver

// Define a default font explicitly for LVGL to use
LV_FONT_DECLARE(lv_font_montserrat_14);

// Temporary buffer for error diffusion dithering (holds grayscale values)
uint8_t *ditherBuffer = NULL;

// Function to perform Floyd-Steinberg dithering
void dither_image(uint8_t *pixels, int width, int height) {
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
        pixels[y * width + x + 1] = min(255, max(0, pixels[y * width + x + 1] + error * 7 / 16));
      
      if (y + 1 < height) {
        if (x > 0)
          pixels[(y + 1) * width + x - 1] = min(255, max(0, pixels[(y + 1) * width + x - 1] + error * 3 / 16));
          
        pixels[(y + 1) * width + x] = min(255, max(0, pixels[(y + 1) * width + x] + error * 5 / 16));
        
        if (x + 1 < width)
          pixels[(y + 1) * width + x + 1] = min(255, max(0, pixels[(y + 1) * width + x + 1] + error * 1 / 16));
      }
    }
  }
}

// Simplified display flush callback with dithering for e-paper display
static void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * px_map)
{
  // For e-paper display, we need a simple approach
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  Serial.printf("Flushing area: x1=%d, y1=%d, x2=%d, y2=%d\n", area->x1, area->y1, area->x2, area->y2);
  
  // Allocate or reallocate the dither buffer if needed
  if (ditherBuffer == NULL || disp->hor_res * disp->ver_res != lvScreenWidth * lvScreenHeight) {
    if (ditherBuffer != NULL) 
      free(ditherBuffer);
    
    ditherBuffer = (uint8_t*)malloc(w * h);
    if (ditherBuffer == NULL) {
      Serial.println("Failed to allocate dither buffer!");
      lv_disp_flush_ready(disp);
      return;
    }
  }
  
  // Convert LVGL color to grayscale
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      lv_color_t pixel = px_map[y * w + x];
      uint8_t gray = lv_color_brightness(pixel);
      ditherBuffer[y * w + x] = gray;
    }
  }
  
  // Perform dithering on the grayscale values
  dither_image(ditherBuffer, w, h);
  
  // Set up the window and draw to display
  display.setPartialWindow(area->x1, area->y1, w, h);
  display.firstPage();
  
  do {
    // Clear with white
    display.fillScreen(GxEPD_WHITE);
    
    // Draw the dithered image
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        // Dithered values are either 0 or 255
        uint16_t color = (ditherBuffer[y * w + x] == 0) ? GxEPD_BLACK : GxEPD_WHITE;
        display.drawPixel(area->x1 + x, area->y1 + y, color);
      }
    }
  } while(display.nextPage());
  
  // Inform LVGL that the flushing is done
  lv_disp_flush_ready(disp);
}

// Free dither buffer when the program exits
void cleanup() {
  if (ditherBuffer != NULL) {
    free(ditherBuffer);
    ditherBuffer = NULL;
  }
}

// Button read callback - simplified
static void read_encoder(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
  // Default state
  data->state = LV_INDEV_STATE_RELEASED;
  data->key = 0;
  
  // Check buttons with debouncing
  if(digitalRead(BUTTON_MENU) == LOW) {
    data->key = LV_KEY_ENTER;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else if(digitalRead(BUTTON_BACK) == LOW) {
    data->key = LV_KEY_ESC;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else if(digitalRead(BUTTON_UP) == LOW) {
    data->key = LV_KEY_UP;
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else if(digitalRead(BUTTON_DOWN) == LOW) {
    data->key = LV_KEY_DOWN;
    data->state = LV_INDEV_STATE_PRESSED;
  }
}
#endif

// Forward declarations for HAL functions
#ifdef __cplusplus
extern "C" {
#endif
void hal_setup(void);
void hal_loop(void);
void hal_cleanup(void);
#ifdef __cplusplus
}
#endif

/**
 * Create a simple demonstration UI
 */
static void create_ui(lv_obj_t * parent)
{
  // Create a title
  lv_obj_t * title = lv_label_create(parent);
  lv_label_set_text(title, "LVGL Demo");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  
  // Create a simple button
  lv_obj_t * btn = lv_btn_create(parent);
  lv_obj_set_size(btn, 120, 50);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
  
  // Add label to button
  lv_obj_t * btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Button");
  lv_obj_center(btn_label);
  
  // Create a slider
  lv_obj_t * slider = lv_slider_create(parent);
  lv_obj_set_size(slider, 160, 20);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_slider_set_value(slider, 50, LV_ANIM_OFF);
}

/**
 * Create a grayscale demonstration UI
 * This function is used by both the Arduino and emulator environments
 */
static void create_grayscale_demo_ui(lv_obj_t * scr, lv_style_t * style_default)
{
  // Set screen background to white
  lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  
  // Create a title
  lv_obj_t * title = lv_label_create(scr);
  if (style_default != NULL) {
    lv_obj_add_style(title, style_default, 0);
  }
  lv_label_set_text(title, "Grayscale Demo");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  
  // Create a group for navigation
  lv_group_t * g = lv_group_create();
  
  // Creating rectangles with different opacities for grayscale demonstration
  for (int i = 0; i < 5; i++) {
    lv_obj_t * rect = lv_obj_create(scr);
    lv_obj_set_size(rect, 150, 20);
    lv_obj_align(rect, LV_ALIGN_TOP_MID, 0, 40 + i * 25);
    
    // Set opacity from 50 to 250 (20% to 100%)
    uint8_t opacity = 50 + i * 50;
    lv_obj_set_style_bg_color(rect, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rect, opacity, 0);
    lv_obj_set_style_border_width(rect, 1, 0);
    lv_obj_set_style_border_color(rect, lv_color_black(), 0);
    
    // Add rectangle to navigation group
    lv_group_add_obj(g, rect);
    
    // Add a label with the opacity percentage
    lv_obj_t * label = lv_label_create(rect);
    if (style_default != NULL) {
      lv_obj_add_style(label, style_default, 0);
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%d%%", opacity * 100 / 255);
    lv_label_set_text(label, buf);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
  }
  
  // Add a circle with gradient for demonstration
  lv_obj_t * circle = lv_obj_create(scr);
  lv_obj_remove_style_all(circle);
  lv_obj_set_size(circle, 80, 80);
  lv_obj_set_style_radius(circle, 40, 0);
  lv_obj_align(circle, LV_ALIGN_BOTTOM_MID, 0, -20);
  
  // Set background color to black
  lv_obj_set_style_bg_color(circle, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  
  // Create and apply a horizontal gradient from black to white
  lv_obj_set_style_bg_grad_color(circle, lv_color_white(), 0);
  lv_obj_set_style_bg_grad_dir(circle, LV_GRAD_DIR_HOR, 0);
  
  // Add the circle to the navigation group
  lv_group_add_obj(g, circle);
  
  // Return the navigation group so both environments can use it
  lv_group_set_default(g);
  
  #if defined(ARDUINO)
  // Initialize keypad input and connect to navigation group
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv.read_cb = read_encoder;
  lv_indev_t * indev = lv_indev_drv_register(&indev_drv);
  lv_indev_set_group(indev, g);
  #else
  // For emulator, connect the keyboard input device to our group
  lv_indev_t * kbd_indev = lv_indev_get_next(NULL);
  while(kbd_indev) {
    if(lv_indev_get_type(kbd_indev) == LV_INDEV_TYPE_KEYPAD) {
      lv_indev_set_group(kbd_indev, g);
      break;
    }
    kbd_indev = lv_indev_get_next(kbd_indev);
  }
  #endif
}

void setup()
{
  #if defined(ARDUINO)
  Serial.begin(115200);
  Serial.println("Starting Watchy LVGL application");

  // Setup button pins with pullups
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  pinMode(BUTTON_MENU, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  
  // Initialize SPI for the display
  SPI.begin(18, 19, 23, DISPLAY_CS); // SCK, MISO, MOSI, SS
  
  // Initialize the e-paper display with simpler parameters
  display.init(115200);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  
  // Show a startup message directly with GxEPD2 commands
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(10, 30);
    display.println("Starting...");
    display.setCursor(10, 60);
    display.println("LVGL on Watchy");
  } while(display.nextPage());
  
  // Allow time for display to refresh
  delay(1000);
  
  // Initialize LVGL
  lv_init();
  Serial.println("LVGL initialized");

  // Initialize LVGL drawing buffer with standard format
  lv_disp_draw_buf_init(&draw_buf, lvBuffer, NULL, lvBufferSize);
  
  // Initialize the display driver
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = lvScreenWidth;
  disp_drv.ver_res = lvScreenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  
  // For monochrome e-paper display specific settings
  disp_drv.rounder_cb = NULL;     // No need for special rounding
  disp_drv.set_px_cb = NULL;      // No special pixel setter needed
  disp_drv.antialiasing = 1;      // Enable antialiasing for better dithering
  disp_drv.full_refresh = 0;      // Use partial refresh mode for better animation
  
  // Register the display
  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
  #else
  // For the emulator, the display is already initialized in hal_setup()
  lv_disp_t * disp = lv_disp_get_default();
  #endif
  
  // Set the theme to monochrome with explicit font
  lv_theme_t * theme = lv_theme_mono_init(disp, false, &lv_font_montserrat_14);
  lv_disp_set_theme(disp, theme);
  
  // Set the default font explicitly to avoid the font issue
  lv_style_t style_default;
  lv_style_init(&style_default);
  lv_style_set_text_font(&style_default, &lv_font_montserrat_14);
  
  // Create a screen with gradient demonstration
  lv_obj_t * scr = lv_obj_create(NULL);
  lv_obj_add_style(scr, &style_default, 0); // Apply the default style to the screen
  
  // Create the grayscale demo UI
  create_grayscale_demo_ui(scr, &style_default);
  
  // Set the screen
  lv_scr_load(scr);
  
  // Serial.println("Setup complete");
}

void loop() 
{
  // Update LVGL system tick
  lv_tick_inc(5);
  
  // Run the LVGL task handler
  lv_task_handler();
  
  // Add a small delay to not overload the CPU
  #if defined(ARDUINO)
    delay(5);
  #endif
}

#if !defined(ARDUINO)
// Main function for SDL emulator environment
int main(int argc, char *argv[]) {
  // Initialize LVGL first
  lv_init();
  
  // Initialize the HAL for SDL - this sets up SDL and the display driver
  hal_setup();
  
  // Create a screen with gradient demonstration (same as in Arduino setup)
  lv_obj_t * scr = lv_obj_create(NULL);
  
  // Set the theme to monochrome with explicit font
  lv_disp_t * disp = lv_disp_get_default();
  lv_theme_t * theme = lv_theme_mono_init(disp, false, &lv_font_montserrat_14);
  lv_disp_set_theme(disp, theme);
  
  // Set the default font explicitly to avoid the font issue
  lv_style_t style_default;
  lv_style_init(&style_default);
  lv_style_set_text_font(&style_default, &lv_font_montserrat_14);
  lv_obj_add_style(scr, &style_default, 0);
  
  // Create the grayscale demo UI using the shared function
  create_grayscale_demo_ui(scr, &style_default);
  
  // Load the screen
  lv_scr_load(scr);
  
  // Main loop for the emulator
  while(1) {
    hal_loop();
    
    // Add a small delay
    usleep(5000);
  }
  
  // Cleanup before exiting
  hal_cleanup();
  return 0;
}
#endif