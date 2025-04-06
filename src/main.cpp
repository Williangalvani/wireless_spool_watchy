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
#include "ui/battery_display.h"

// No need for separate include, lvgl.h already includes what we need

// Include different headers based on environment
#if defined(ARDUINO)
  #include <Arduino.h>
  #include <SPI.h>
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Fonts/FreeMonoBold9pt7b.h>
  #include "../hal/esp32/displays/LGFX_WATCHY_EPAPER.hpp"
  #include <WiFi.h>
  #include <ArduinoOTA.h>
  #include <ESPmDNS.h>
#else
  // SDL emulator environment
  #include "../hal/sdl2/app_hal.h"
  
  // Define the same dimensions as the SDL HAL to prevent segfault
  #define DISPLAY_WIDTH 480
  #define DISPLAY_HEIGHT 320
  #define DISPLAY_BITS_PER_PIXEL 1
#endif

// WiFi credentials for OTA updates
#if defined(ARDUINO)
  #define WIFI_SSID "Pão de Batata"
  #define WIFI_PASSWORD "bananaamassadinha"
  #define OTA_HOSTNAME "watchy-lvgl"
#endif

// Define a default font explicitly for LVGL to use
// We don't need to redeclare these, they're in lv_font.h already included via lvgl.h
// Just using them directly

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

// Setup OTA updates
void setupOTA() {
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Wait for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    return;
  }
  
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Set up mDNS responder
  if (MDNS.begin(OTA_HOSTNAME)) {
    Serial.println("mDNS responder started");
    Serial.printf("You can update firmware using: %s.local\n", OTA_HOSTNAME);
  }
  
  // Initialize OTA
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting...");
    // Stop LVGL to prevent display conflicts during update
    if (ditherBuffer) {
      free(ditherBuffer);
      ditherBuffer = NULL;
    }
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update complete!");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  
  ArduinoOTA.begin();
  Serial.println("OTA setup complete");
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

// Define fonts using the built-in LVGL fonts
#ifndef LV_FONT_MONTSERRAT_MEDIUM_29
#define LV_FONT_MONTSERRAT_MEDIUM_29 &lv_font_montserrat_28
#endif

#ifndef LV_FONT_MONTSERRAT_MEDIUM_16
#define LV_FONT_MONTSERRAT_MEDIUM_16 &lv_font_montserrat_16
#endif

/**
 * Create the screen UI as specified
 * This function is used by both the Arduino and emulator environments
 */
static void setup_scr_screen(lv_obj_t * scr, lv_style_t * style_default)
{
  // Set screen background to white
  lv_obj_set_size(scr, 200, 200);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(scr, lv_color_white(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
  
  // Create "Spool" label
  lv_obj_t * screen_label_1 = lv_label_create(scr);
  lv_label_set_text(screen_label_1, "Spool");
  lv_label_set_long_mode(screen_label_1, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(screen_label_1, 4, 14);
  lv_obj_set_size(screen_label_1, 192, 42);

  // Style for screen_label_1
  lv_obj_set_style_border_width(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(screen_label_1, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(screen_label_1, LV_FONT_MONTSERRAT_MEDIUM_29, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(screen_label_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_letter_space(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_line_space(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(screen_label_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(screen_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

  // Create "Vehicle" label
  lv_obj_t * screen_label_2 = lv_label_create(scr);
  lv_label_set_text(screen_label_2, "Vehicle\n");
  lv_label_set_long_mode(screen_label_2, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(screen_label_2, 4, 56);
  lv_obj_set_size(screen_label_2, 194, 29);

  // Style for screen_label_2
  lv_obj_set_style_border_width(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(screen_label_2, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(screen_label_2, LV_FONT_MONTSERRAT_MEDIUM_29, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(screen_label_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_letter_space(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_line_space(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(screen_label_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(screen_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

  // Create "Uptime" label
  lv_obj_t * screen_label_3 = lv_label_create(scr);
  lv_label_set_text(screen_label_3, "Uptime\n");
  lv_label_set_long_mode(screen_label_3, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(screen_label_3, 4, 165);
  lv_obj_set_size(screen_label_3, 122, 30);

  // Style for screen_label_3
  lv_obj_set_style_border_width(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(screen_label_3, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(screen_label_3, LV_FONT_MONTSERRAT_MEDIUM_16, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(screen_label_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_letter_space(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_line_space(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(screen_label_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_right(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_shadow_width(screen_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  
  // Create WiFi status label
  lv_obj_t * wifi_label = lv_label_create(scr);
  lv_label_set_text(wifi_label, "WiFi: --");
  lv_label_set_long_mode(wifi_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(wifi_label, 4, 135);
  lv_obj_set_size(wifi_label, 192, 30);
  
  // Style for wifi_label
  lv_obj_set_style_border_width(wifi_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(wifi_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(wifi_label, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(wifi_label, LV_FONT_MONTSERRAT_MEDIUM_16, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(wifi_label, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
  
  // Create battery display label in the top-right corner
  lv_obj_t * battery_label = lv_label_create(scr);
  lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -10, 5);
  lv_obj_set_size(battery_label, 60, 20);
  
  // Apply style if provided
  if (style_default != nullptr) {
    lv_obj_add_style(battery_label, style_default, 0);
    lv_obj_add_style(wifi_label, style_default, 0);
  }
  lv_obj_set_style_text_color(battery_label, lv_color_black(), LV_PART_MAIN);
  
  // Set the initial text (it will be updated in the loop)
  float voltage = BatteryDisplay::getInstance()->getVoltage();
  char buffer[16];
  int voltsInt = (int)voltage;
  int voltsDec = (int)((voltage - voltsInt) * 100);
  snprintf(buffer, sizeof(buffer), "%d.%02dV", voltsInt, voltsDec);
  Serial.printf("Initial battery: %s (raw: %.2f)\n", buffer, voltage);
  lv_label_set_text(battery_label, buffer);
  
  // Store both labels in the screen's user data for easy access
  struct {
    lv_obj_t* battery;
    lv_obj_t* wifi;
  } *labels = (decltype(labels))lv_mem_alloc(sizeof(*labels));
  
  if (labels) {
    labels->battery = battery_label;
    labels->wifi = wifi_label;
    lv_obj_set_user_data(scr, labels);
  }
  
  // Update layout
  lv_obj_update_layout(scr);
}

// Function to update the battery display
static void update_battery_display() {
  lv_obj_t* scr = lv_scr_act();
  struct {
    lv_obj_t* battery;
    lv_obj_t* wifi;
  } *labels = (decltype(labels))lv_obj_get_user_data(scr);
  
  if (labels && labels->battery && BatteryDisplay::getInstance()->shouldUpdate()) {
    float voltage = BatteryDisplay::getInstance()->getVoltage();
    char buffer[16];
    int voltsInt = (int)voltage;
    int voltsDec = (int)((voltage - voltsInt) * 100);
    snprintf(buffer, sizeof(buffer), "%d.%02dV", voltsInt, voltsDec);
    Serial.printf("Battery voltage: %s (raw: %.2f)\n", buffer, voltage);
    lv_label_set_text(labels->battery, buffer);
  }
}

// Function to update WiFi status display
static void update_wifi_display() {
  lv_obj_t* scr = lv_scr_act();
  struct {
    lv_obj_t* battery;
    lv_obj_t* wifi;
  } *labels = (decltype(labels))lv_obj_get_user_data(scr);
  
  if (labels && labels->wifi) {
    char buffer[64];
    if (WiFi.status() == WL_CONNECTED) {
      lv_snprintf(buffer, sizeof(buffer), "WiFi: Connected\nIP: %s", WiFi.localIP().toString().c_str());
    } else {
      lv_snprintf(buffer, sizeof(buffer), "WiFi: Disconnected");
    }
    lv_label_set_text(labels->wifi, buffer);
  }
}

#if defined(ARDUINO)
// Arduino setup function
void setup()
{
  Serial.begin(115200);
  Serial.println("Starting Watchy LVGL application");

  // Setup ADC for battery monitoring
  analogReadResolution(12);  // Set ADC resolution to 12-bit
  
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
  
  // Set up OTA update functionality
  setupOTA();
  
  // Set the theme to monochrome with explicit font
  lv_theme_t * theme = lv_theme_mono_init(disp, false, &lv_font_montserrat_14);
  lv_disp_set_theme(disp, theme);
  
  // Set the default font explicitly to avoid the font issue
  lv_style_t style_default;
  lv_style_init(&style_default);
  lv_style_set_text_font(&style_default, &lv_font_montserrat_14);
  
  // Create a screen with the new UI
  lv_obj_t * scr = lv_obj_create(NULL);
  lv_obj_add_style(scr, &style_default, 0); // Apply the default style to the screen
  
  // Create the new screen UI
  setup_scr_screen(scr, &style_default);
  
  // Set the screen
  lv_scr_load(scr);
}

// Arduino loop function
void loop() 
{
  // Update LVGL system tick
  lv_tick_inc(5);
  
  // Run the LVGL task handler
  lv_task_handler();
  
  // Update battery display
  update_battery_display();
  
  // Update WiFi display
  update_wifi_display();
  
  // Handle OTA updates
  ArduinoOTA.handle();
  
  // Add a small delay to not overload the CPU
  delay(5);
}

#else
// Main function for SDL emulator environment
int main(int argc, char *argv[]) {
  // Initialize LVGL first
  lv_init();
  
  // Initialize the HAL for SDL - this sets up SDL and the display driver
  hal_setup();
  
  // Create a screen with the new UI
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
  
  // Create the new screen UI using the shared function
  setup_scr_screen(scr, &style_default);
  
  // Load the screen
  lv_scr_load(scr);
  
  // Main loop for the emulator
  while(1) {
    // Run the HAL loop (handles SDL events and updates)
    hal_loop();
    
    // LVGL timing and task handling
    lv_tick_inc(5);
    lv_task_handler();
    
    // Update battery display
    update_battery_display();
    
    // Add a small delay
    usleep(5000);
  }
  
  // Cleanup before exiting
  hal_cleanup();
  return 0;
}
#endif