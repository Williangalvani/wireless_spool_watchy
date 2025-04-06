/**
  ******************************************************************************
  * @file    main.cpp
  * @author  LVGL for Watchy e-paper display
  * @brief   Main program for Watchy with LVGL
  ******************************************************************************
*/

#include <Arduino.h>
#include <lvgl.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "../hal/esp32/displays/LGFX_WATCHY_EPAPER.hpp"

// Buffer size calculation for the display
const uint32_t lvScreenWidth = DISPLAY_WIDTH;
const uint32_t lvScreenHeight = DISPLAY_HEIGHT;
const uint32_t lvBufferSize = lvScreenWidth * lvScreenHeight / 8; // 1 bit per pixel

static uint8_t lvBuffer[lvBufferSize]; // Buffer for LVGL - using uint8_t for monochrome display

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

// Simplified display flush callback for e-paper display
static void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * px_map)
{
  // For e-paper display, we need a simple approach
  // Show directly on the display for debugging
  display.setFullWindow(); // Use full display refresh for e-paper
  display.firstPage();
  
  do {
    display.fillScreen(GxEPD_WHITE); // Clear background
    
    // Draw each pixel directly from the LVGL buffer
    for(int y = 0; y < lvScreenHeight; y++) {
      for(int x = 0; x < lvScreenWidth; x++) {
        int bufferPos = (y * lvScreenWidth + x) / 8;
        int bitPos = 7 - (y * lvScreenWidth + x) % 8; // Most significant bit first
        
        if(lvBuffer[bufferPos] & (1 << bitPos)) {
          display.drawPixel(x, y, GxEPD_BLACK);
        }
      }
    }
  } while(display.nextPage());
  
  // Inform LVGL that the flushing is done
  lv_disp_flush_ready(disp);
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

void setup()
{
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
  
  // Initialize LVGL drawing buffer with 1-bit per pixel for monochrome
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
  
  // Register the display
  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
  
  // Set the theme to monochrome
  lv_theme_t * theme = lv_theme_mono_init(disp, false, NULL);
  lv_disp_set_theme(disp, theme);
  
  // Initialize keypad input
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv.read_cb = read_encoder;
  lv_indev_t * indev = lv_indev_drv_register(&indev_drv);
  
  // Create a default group to handle button navigation
  lv_group_t * g = lv_group_create();
  lv_group_set_default(g);
  lv_indev_set_group(indev, g);
  
  // Create a basic screen with shapes only - no labels
  lv_obj_t * scr = lv_obj_create(NULL);
  
  // Create rectangles instead of buttons with text
  lv_obj_t * rect1 = lv_obj_create(scr);
  lv_obj_set_size(rect1, 80, 40);
  lv_obj_align(rect1, LV_ALIGN_CENTER, 0, -40);
  
  lv_obj_t * rect2 = lv_obj_create(scr);
  lv_obj_set_size(rect2, 80, 40);
  lv_obj_align(rect2, LV_ALIGN_CENTER, 0, 40);
  
  // Add objects to the group for navigation
  lv_group_add_obj(g, rect1);
  lv_group_add_obj(g, rect2);
  
  // Set the screen
  lv_scr_load(scr);
  
  Serial.println("Setup complete");
}

void loop() 
{
  // Update LVGL system tick
  lv_tick_inc(5);
  
  // Run the LVGL task handler
  lv_task_handler();
  
  // Add a small delay to not overload the CPU
  delay(5);
}