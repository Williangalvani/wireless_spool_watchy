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
  
  // Define the same dimensions as the Watchy for the emulator
  #define DISPLAY_WIDTH 200
  #define DISPLAY_HEIGHT 200
  #define DISPLAY_BITS_PER_PIXEL 1
#endif

// Define a default font explicitly for LVGL to use
LV_FONT_DECLARE(lv_font_montserrat_14);

// Buffer size calculation for the display
const uint32_t lvScreenWidth = DISPLAY_WIDTH;
const uint32_t lvScreenHeight = DISPLAY_HEIGHT;
const uint32_t lvBufferSize = lvScreenWidth * lvScreenHeight / 10; // 1/10 of screen for drawing buffer

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
  lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, lvBuffer, NULL, lvBufferSize);
  
  // Initialize the display driver
  lv_disp_drv_t disp_drv;
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
  
  // Set screen background to white
  lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  
  // Create the UI
  create_ui(scr);
  
  // Set the screen
  lv_scr_load(scr);
  
  Serial.println("Setup complete");
#else
  // Initialize LVGL first
  printf("Initializing LVGL...\n");
  lv_init();
  
  // Initialize HAL after LVGL
  printf("Initializing HAL...\n");
  hal_setup();
  
  // Create simple UI
  printf("Creating UI...\n");
  lv_obj_t * scr = lv_scr_act();
  create_ui(scr);
  
  printf("Entering main loop...\n");
  // Main loop with proper error handling
  try {
    while(1) {
      hal_loop();  // This already calls lv_task_handler()
      usleep(5 * 1000); // 5ms delay
    }
  } 
  catch (const std::exception& e) {
    printf("Error in main loop: %s\n", e.what());
  }
  catch (...) {
    printf("Unknown error in main loop\n");
  }
  
  // Ensure proper cleanup
  printf("Exiting application, cleaning up...\n");
  hal_cleanup();
#endif
}

void loop() 
{
#if defined(ARDUINO)
  // Process LVGL tasks periodically
  lv_task_handler();
  delay(5);
#else
  // The emulator's main loop is handled in hal_loop()
  hal_loop();
#endif
}

#if defined(ARDUINO)
// Arduino environment uses setup() and loop() functions
#else
// Main function for SDL environment
int main(void)
{
  printf("Starting LVGL application with SDL emulator...\n");
  
  // Initialize LVGL first
  printf("Initializing LVGL...\n");
  lv_init();
  
  // Initialize HAL after LVGL
  printf("Initializing HAL...\n");
  hal_setup();
  
  // Create simple UI
  printf("Creating UI...\n");
  lv_obj_t * scr = lv_scr_act();
  create_ui(scr);
  
  printf("Entering main loop...\n");
  // Main loop with proper error handling
  try {
    while(1) {
      hal_loop();  // This already calls lv_task_handler()
      usleep(5 * 1000); // 5ms delay
    }
  } 
  catch (const std::exception& e) {
    printf("Error in main loop: %s\n", e.what());
  }
  catch (...) {
    printf("Unknown error in main loop\n");
  }
  
  // Ensure proper cleanup
  printf("Exiting application, cleaning up...\n");
  hal_cleanup();
  
  return 0;
}
#endif