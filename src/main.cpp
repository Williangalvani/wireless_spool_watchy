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
const uint32_t lvBufferSize = lvScreenWidth * lvScreenHeight / 10; // 1/10 of screen for drawing buffer

static lv_color_t lvBuffer[lvBufferSize]; // Buffer for LVGL - using color_t for standard LVGL format

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

// Simplified display flush callback for e-paper display
static void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * px_map)
{
  // For e-paper display, we need a simple approach
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  Serial.printf("Flushing area: x1=%d, y1=%d, x2=%d, y2=%d\n", area->x1, area->y1, area->x2, area->y2);
  
  // Use partial window update for better performance
  display.setPartialWindow(area->x1, area->y1, w, h);
  display.firstPage();
  
  do {
    // Clear area with white
    display.fillScreen(GxEPD_WHITE);
    
    // Process each pixel in the area
    for(int y = 0; y < h; y++) {
      for(int x = 0; x < w; x++) {
        // Get the pixel value from px_map
        lv_color_t pixel = px_map[y * w + x];
        
        // For monochrome display, convert LVGL color to e-paper color
        // In LVGL, 0 is black, 1 is white (for monochrome)
        // In GxEPD2, GxEPD_BLACK is black (0), GxEPD_WHITE is white (1)
        uint16_t color = lv_color_brightness(pixel) > 128 ? GxEPD_WHITE : GxEPD_BLACK;
        
        // Draw the pixel on the e-paper display
        display.drawPixel(area->x1 + x, area->y1 + y, color);
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
  disp_drv.antialiasing = 0;      // No antialiasing for e-paper
  disp_drv.full_refresh = 1;      // Use full refresh mode
  
  // Register the display
  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
  
  // Set the theme to monochrome with explicit font
  lv_theme_t * theme = lv_theme_mono_init(disp, false, &lv_font_montserrat_14);
  lv_disp_set_theme(disp, theme);
  
  // Set the default font explicitly to avoid the font issue
  lv_style_t style_default;
  lv_style_init(&style_default);
  lv_style_set_text_font(&style_default, &lv_font_montserrat_14);
  
  // Create a screen with black/white contrast settings
  lv_obj_t * scr = lv_obj_create(NULL);
  lv_obj_add_style(scr, &style_default, 0); // Apply the default style to the screen
  
  // Set screen background to white
  lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  
  // Create a black rectangle for contrast
  lv_obj_t * rect1 = lv_obj_create(scr);
  lv_obj_set_size(rect1, 100, 50);
  lv_obj_align(rect1, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_bg_color(rect1, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(rect1, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rect1, 2, 0);
  lv_obj_set_style_border_color(rect1, lv_color_white(), 0);
  
  // Add the rectangle to the group for navigation
  lv_group_t * g = lv_group_create();
  lv_group_add_obj(g, rect1);
  
  // Create a white label on the black rectangle for contrast
  lv_obj_t * label1 = lv_label_create(rect1);
  lv_obj_add_style(label1, &style_default, 0);
  lv_label_set_text(label1, "Watchy");
  lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(label1, lv_color_white(), 0);
  
  // Create a white rectangle for contrast
  lv_obj_t * rect2 = lv_obj_create(scr);
  lv_obj_set_size(rect2, 100, 50);
  lv_obj_align(rect2, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(rect2, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(rect2, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rect2, 2, 0);
  lv_obj_set_style_border_color(rect2, lv_color_black(), 0);
  
  // Add the second rectangle to the navigation group
  lv_group_add_obj(g, rect2);
  
  // Create a black label on the white rectangle for contrast
  lv_obj_t * label2 = lv_label_create(rect2);
  lv_obj_add_style(label2, &style_default, 0);
  lv_label_set_text(label2, "LVGL");
  lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(label2, lv_color_black(), 0);
  
  // Initialize keypad input and connect to navigation group
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_KEYPAD;
  indev_drv.read_cb = read_encoder;
  lv_indev_t * indev = lv_indev_drv_register(&indev_drv);
  lv_indev_set_group(indev, g);
  
  // Set the default group
  lv_group_set_default(g);
  
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