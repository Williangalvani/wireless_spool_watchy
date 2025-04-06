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

// Buffer to store the entire screen content for comparison
static uint8_t *global_display_buffer = NULL;
static bool global_buffer_initialized = false;

// Flag to enable/disable verbose logging - set to true to see detailed pixel changes
static bool verbose_pixel_logging = true;
// Limit the number of pixel changes to log to avoid flooding Serial
static const int max_pixel_changes_to_log = 10;

// Temporary buffer for binary thresholding (holds grayscale values)
uint8_t *thresholdBuffer = NULL;

// Simplified display flush callback with simple thresholding for e-paper display
static void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * px_map)
{
  // For e-paper display, we need a simple approach
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  // Serial.printf("Flushing area: x1=%d, y1=%d, x2=%d, y2=%d\n", area->x1, area->y1, area->x2, area->y2);
  
  // Allocate or reallocate the threshold buffer if needed
  if (thresholdBuffer == NULL || disp->hor_res * disp->ver_res != lvScreenWidth * lvScreenHeight) {
    if (thresholdBuffer != NULL) 
      free(thresholdBuffer);
    
    thresholdBuffer = (uint8_t*)malloc(w * h);
    if (thresholdBuffer == NULL) {
      Serial.println("Failed to allocate threshold buffer!");
      lv_disp_flush_ready(disp);
      return;
    }
  }
  
  // Initialize the global display buffer if not already allocated
  // This buffer stores the ENTIRE screen state, not just the current update area
  if (global_display_buffer == NULL) {
    // Allocate buffer for the entire screen
    global_display_buffer = (uint8_t*)calloc(disp->hor_res * disp->ver_res, sizeof(uint8_t));
    if (global_display_buffer == NULL) {
      Serial.println("Failed to allocate global display buffer!");
      // Continue without comparison as this is first-time initialization
    } else {
      Serial.printf("Allocated global buffer for entire %dx%d screen\n", disp->hor_res, disp->ver_res);
      // Initialize to all white (255)
      memset(global_display_buffer, 255, disp->hor_res * disp->ver_res);
      global_buffer_initialized = true;
    }
  }
  
  // Convert LVGL color to grayscale with simple thresholding
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      lv_color_t pixel = px_map[y * w + x];
      uint8_t gray = lv_color_brightness(pixel);
      
      // Simple binary thresholding (no dithering)
      thresholdBuffer[y * w + x] = (gray < 128) ? 0 : 255;
    }
  }
  
  // Check if the display content has changed
  bool has_changes = false;
  int change_count = 0;
  
  if (global_buffer_initialized) {
    // Compare the new thresholded content with the previous global buffer content
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        // Calculate global buffer position for this pixel
        int global_x = area->x1 + x;
        int global_y = area->y1 + y;
        
        // Skip invalid coordinates (should never happen)
        if (global_x >= disp->hor_res || global_y >= disp->ver_res) {
          continue;
        }
        
        // Get the previous value from the global buffer
        int global_idx = global_y * disp->hor_res + global_x;
        uint8_t prev_value = global_display_buffer[global_idx];
        uint8_t new_value = thresholdBuffer[y * w + x];
        
        // Compare values
        if (new_value != prev_value) {
          has_changes = true;
          change_count++;
          
          // Log detailed pixel change information if verbose logging is enabled
          if (verbose_pixel_logging && change_count <= max_pixel_changes_to_log) {
            Serial.printf("Pixel change at (%d,%d): %d -> %d\n", 
                         global_x, global_y, 
                         prev_value, 
                         new_value);
          }
          
          // Update the global buffer with the new value immediately
          global_display_buffer[global_idx] = new_value;
        }
      }
    }
    
    // Log total changes
    if (has_changes) {
      Serial.printf("Total pixel changes: %d out of %d pixels\n", change_count, w * h);
    }
  } else {
    // If global buffer doesn't exist, assume changes
    has_changes = true;
    Serial.println("First update - no global buffer for comparison");
  }
  
  // Only update the display if content has changed
  if (has_changes) {
    Serial.println("Content changed, updating display");
    
    // Set up the window and draw to display
    display.setPartialWindow(area->x1, area->y1, w, h);
    display.firstPage();
    
    do {
      // Clear with white
      display.fillScreen(GxEPD_WHITE);
      
      // Draw the thresholded image
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          // Values are either 0 or 255
          uint16_t color = (thresholdBuffer[y * w + x] == 0) ? GxEPD_BLACK : GxEPD_WHITE;
          display.drawPixel(area->x1 + x, area->y1 + y, color);
        }
      }
    } while(display.nextPage());
  } else {
    // Serial.println("No changes, skipping display update");
  }
  
  // Inform LVGL that the flushing is done
  lv_disp_flush_ready(disp);
}

// Free buffers when the program exits
void cleanup() {
  if (thresholdBuffer != NULL) {
    free(thresholdBuffer);
    thresholdBuffer = NULL;
  }
  
  if (global_display_buffer != NULL) {
    free(global_display_buffer);
    global_display_buffer = NULL;
    global_buffer_initialized = false;
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
    if (thresholdBuffer) {
      free(thresholdBuffer);
      thresholdBuffer = NULL;
    }
    
    if (global_display_buffer) {
      free(global_display_buffer);
      global_display_buffer = NULL;
      global_buffer_initialized = false;
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

// Define fonts using the built-in LVGL fonts
#ifndef LV_FONT_MONTSERRAT_MEDIUM_48
#define LV_FONT_MONTSERRAT_MEDIUM_48 &lv_font_montserrat_48
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
  lv_obj_set_style_text_font(screen_label_1, LV_FONT_MONTSERRAT_MEDIUM_48, LV_PART_MAIN|LV_STATE_DEFAULT);
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
  lv_obj_set_pos(screen_label_3, 4, 170);
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
  lv_obj_set_pos(wifi_label, 4, 120);
  lv_obj_set_size(wifi_label, 192, 40);
  
  // Style for wifi_label
  lv_obj_set_style_border_width(wifi_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(wifi_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(wifi_label, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(wifi_label, LV_FONT_MONTSERRAT_MEDIUM_16, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(wifi_label, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
  

  
  // Apply style if provided
  if (style_default != nullptr) {
    lv_obj_add_style(wifi_label, style_default, 0);
  }
  
  // Set the initial text (it will be updated in the loop)
  float voltage = BatteryDisplay::getInstance()->getVoltage();
  char buffer[16];
  int voltsInt = (int)voltage;
  int voltsDec = (int)((voltage - voltsInt) * 100);
  snprintf(buffer, sizeof(buffer), "%d.%02dV", voltsInt, voltsDec);
  Serial.printf("Initial battery: %s (raw: %.2f)\n", buffer, voltage);
  lv_label_set_text(screen_label_1, buffer);
  
  // Store both labels in the screen's user data for easy access
  struct {
    lv_obj_t* battery;
    lv_obj_t* wifi;
    lv_obj_t* mavlink;
  } *labels = (decltype(labels))lv_mem_alloc(sizeof(*labels));
  
  if (labels) {
    labels->wifi = wifi_label;
    labels->mavlink = NULL;
    lv_obj_set_user_data(scr, labels);
  }
  
  // Create mavlink service label
  lv_obj_t * mavlink_label = lv_label_create(scr);
  lv_label_set_text(mavlink_label, "Mavlink: Searching...");
  lv_label_set_long_mode(mavlink_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(mavlink_label, 4, 85);
  lv_obj_set_size(mavlink_label, 192, 30);

  // Style for mavlink_label
  lv_obj_set_style_border_width(mavlink_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_radius(mavlink_label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(mavlink_label, lv_color_black(), LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(mavlink_label, LV_FONT_MONTSERRAT_MEDIUM_16, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(mavlink_label, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(mavlink_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);

  // Add style if provided
  if (style_default != nullptr) {
    lv_obj_add_style(mavlink_label, style_default, 0);
  }

  // Store the mavlink label in the screen's user data
  if (labels) {
    labels->mavlink = mavlink_label;
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
    lv_obj_t* mavlink;
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
    lv_obj_t* mavlink;
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

// Add function to update mavlink service display
static void update_mavlink_display() {
  lv_obj_t* scr = lv_scr_act();
  struct {
    lv_obj_t* battery;
    lv_obj_t* wifi;
    lv_obj_t* mavlink;
  } *labels = (decltype(labels))lv_obj_get_user_data(scr);
  
  if (labels && labels->mavlink) {
    static uint32_t last_mavlink_check = 0;
    uint32_t current_time = millis();
    
    // Check for mavlink service every 5 seconds
    if (current_time - last_mavlink_check > 5000) {
      char buffer[64];
      bool serviceFound = false;

      // Query mDNS for mavlink service
      int n = MDNS.queryService("mavlink", "udp");
      if (n > 0) {
        // Service found - display the first one
        lv_snprintf(buffer, sizeof(buffer), "%s", 
                   MDNS.IP(0).toString().c_str());
        serviceFound = true;
      } else {
        lv_snprintf(buffer, sizeof(buffer), "Mavlink: Not found");
      }
      Serial.printf("Mavlink: %d devices, %s ", n, MDNS.IP(0).toString().c_str());
      
      lv_label_set_text(labels->mavlink, buffer);
      last_mavlink_check = current_time;
    }
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
  
  // Update mavlink service display
  update_mavlink_display();
  
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
    
    // Update mavlink service display
    update_mavlink_display();
    
    // Add a small delay
    usleep(5000);
  }
  
  // Cleanup before exiting
  hal_cleanup();
  return 0;
}
#endif