/**
  ******************************************************************************
  * @file    main.cpp
  * @author  Direct GxEPD2 implementation for Watchy e-paper display
  * @brief   Main program for Watchy with GxEPD2 direct drawing
  ******************************************************************************
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>        // Add proportional Sans font
#include <Fonts/FreeSansBold9pt7b.h>    // Add bold proportional Sans font
#include <Fonts/FreeSansBold12pt7b.h>   // Add larger bold proportional Sans font
#include "../hal/esp32/displays/LGFX_WATCHY_EPAPER.hpp"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "esp_sleep.h"

// WiFi icon bitmap (20x20 pixels)
const unsigned char WIFI_ICON[] PROGMEM = {
  0b00000000, 0b00000000, 0b00000000, // ....................
  0b00000111, 0b11111110, 0b00000000, // .....XXXXXXXX.......
  0b00011111, 0b11111111, 0b10000000, // ...XXXXXXXXXXXX.....
  0b00111100, 0b00000011, 0b11000000, // ..XXXX......XXXX....
  0b01110000, 0b00000000, 0b11100000, // .XXX..........XXX...
  0b01100000, 0b00000000, 0b01100000, // .XX............XX...
  0b01000000, 0b00000000, 0b00100000, // .X..............X...
  0b00000011, 0b11111100, 0b00000000, // ......XXXXXX........
  0b00001111, 0b11111111, 0b00000000, // ....XXXXXXXXXX......
  0b00011100, 0b00000011, 0b10000000, // ...XXX......XXX.....
  0b00111000, 0b00000001, 0b11000000, // ..XXX........XXX....
  0b00110000, 0b00000000, 0b11000000, // ..XX..........XX....
  0b00000001, 0b11111000, 0b00000000, // .......XXXXX........
  0b00000111, 0b11111110, 0b00000000, // .....XXXXXXXX.......
  0b00001110, 0b00000111, 0b00000000, // ....XXX....XXX......
  0b00001100, 0b00000011, 0b00000000, // ....XX......XX......
  0b00000000, 0b11110000, 0b00000000, // ........XXXX........
  0b00000001, 0b11111000, 0b00000000, // .......XXXXX........
  0b00000001, 0b11111000, 0b00000000, // .......XXXXX........
  0b00000000, 0b11110000, 0b00000000  // ........XXXX........
};

// WiFi credentials for OTA updates
// #define WIFI_SSID "Pão de Batata"
#define WIFI_SSID "spool-iot"
#define WIFI_PASSWORD "bananaamassadinha"
#define OTA_HOSTNAME "watchy-lvgl2"

// Web server on port 80
WebServer server(80);

// Variables to store screen data for web server
String vehicleNames[3];
float vehicleVoltages[3];
int vehicleCount = 0;
float batteryVoltage = 0.0f;
bool wifiConnected = false;
unsigned long deviceUptime = 0;
String ipAddress = "0.0.0.0"; // Add variable to store IP address

// Track WiFi connection attempts
unsigned long wifiDisconnectedTime = 0;
int reconnectionAttempts = 0;
const int MAX_RECONNECTION_ATTEMPTS = 1; // Try 5 times before sleeping
const unsigned long WIFI_DEEP_SLEEP_DURATION = 60e6; // 60 seconds in microseconds

// Create the display instance
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(
    DISPLAY_CS,
    DISPLAY_DC,
    DISPLAY_RESET,
    DISPLAY_BUSY
  )
);

// Define custom fonts for UI elements
#define LARGE_FONT FreeSansBold18pt7b
#define MEDIUM_FONT FreeSansBold12pt7b
#define SMALL_FONT FreeSansBold9pt7b

// Battery monitoring class
class BatteryDisplay {
private:
  static BatteryDisplay* instance;
  float currentVoltage;
  unsigned long lastUpdateTime;
  const unsigned long updateIntervalMs = 60000; // Update every minute
  const uint8_t batteryPin = 34; // ADC pin connected to battery
  
  BatteryDisplay() : currentVoltage(0.0f), lastUpdateTime(0) {
    analogReadResolution(12); // Set ADC resolution to 12-bit
    updateVoltage();
  }

public:
  static BatteryDisplay* getInstance() {
    if (instance == nullptr) {
      instance = new BatteryDisplay();
    }
    return instance;
  }
  
  float getVoltage() {
    return currentVoltage;
  }
  
  bool shouldUpdate() {
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime >= updateIntervalMs) {
      updateVoltage();
      lastUpdateTime = currentTime;
      return true;
    }
    return false;
  }
  
  void updateVoltage() {
    // Analog read and calculate voltage
    // The battery voltage is divided by 2 via a voltage divider
    // ADC range is 0-4095 for 0-3.3V
    int adcValue = analogRead(batteryPin);
    float voltage = adcValue * 3.3 * 2 / 4095.0;
    currentVoltage = voltage * 1.0678;
    Serial.printf("Battery ADC: %d, Voltage: %.2fV\n", adcValue, voltage);
  }
};

// Initialize static instance
BatteryDisplay* BatteryDisplay::instance = nullptr;

// Function to get battery voltage from Mavlink HTTP API
float getMavlinkBatteryVoltage(const String& vehicleIP) {
  float batteryVoltage = -1.0f; // Default value indicating failure
  
  if (vehicleIP.length() == 0 || vehicleIP == "Not found" || vehicleIP == "0.0.0.0") {
    Serial.println("Invalid vehicle IP address");
    return batteryVoltage;
  }
  
  HTTPClient http;
  
  // Construct the URL for the battery voltage endpoint
  String url = "http://" + vehicleIP + ":6040/v1/mavlink/vehicles/1/components/1/messages/BATTERY_STATUS/message/voltages/0";
  
  Serial.println("Making request to: " + url);
  http.begin(url);
  
  // Set timeout for the request
  http.setTimeout(5000); // 5 second timeout
  
  // Send GET request
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println("Payload: " + payload);
    
    // Parse the plain text number response
    // No need for JSON parsing since the response is just a number
    int millivolts = payload.toInt();
    if (millivolts > 0) {
      // The API returns millivolts, convert to volts
      batteryVoltage = millivolts / 1000.0f;
      Serial.printf("Battery voltage: %.2f V\n", batteryVoltage);
    } else {
      Serial.println("Failed to parse voltage value from response");
    }
  } else {
    // Improved error reporting
    String errorMsg;
    switch (httpResponseCode) {
      case -1:
        errorMsg = "Connection failed";
        break;
      case -2:
        errorMsg = "Connection lost";
        break;
      case -3:
        errorMsg = "Connection timed out";
        break;
      case -4:
        errorMsg = "Server sent invalid response";
        break;
      case -5:
        errorMsg = "Connection refused";
        break;
      case -6:
        errorMsg = "Invalid server response";
        break;
      case -7:
        errorMsg = "Failed to allocate stream";
        break;
      case -8:
        errorMsg = "Not enough memory";
        break;
      case -9:
        errorMsg = "Invalid HTTP response";
        break;
      case -10:
        errorMsg = "More data pending";
        break;
      case -11:
        errorMsg = "Connection timeout";
        break;
      default:
        errorMsg = "Unknown error";
    }
    Serial.printf("HTTP request failed, error: %d (%s)\n", httpResponseCode, errorMsg.c_str());
  }
  
  http.end();
  
  return batteryVoltage;
}

// Function to get vehicle name from the API
String getVehicleName(const String& vehicleIP) {
  String vehicleName = "Vehicle";  // Default name if API call fails
  
  if (vehicleIP.length() == 0 || vehicleIP == "0.0.0.0") {
    Serial.println("Invalid vehicle IP for name lookup");
    return vehicleName;
  }
  
  HTTPClient http;
  
  // Construct the URL for the vehicle name endpoint
  String url = "http://" + vehicleIP + ":9111/v1.0/vehicle_name";
  
  Serial.println("Getting vehicle name from: " + url);
  http.begin(url);
  
  // Set timeout for the request
  http.setTimeout(3000); // 3 second timeout
  
  // Send GET request
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpResponseCode);
    String payload = http.getString();
    Serial.println("Name payload: " + payload);
    
    // Trim whitespace and use the response as vehicle name
    vehicleName = payload;
    vehicleName.trim();
    
    // Remove any double quotes from the name
    for (int i = 0; i < vehicleName.length(); i++) {
      if (vehicleName[i] == '"') {
        vehicleName.remove(i, 1);
        i--; // Adjust index after removal
      }
    }
    
    // If empty response, use default
    if (vehicleName.length() == 0) {
      vehicleName = "Vehicle";
    }
  } else {
    // Improved error reporting
    String errorMsg;
    switch (httpResponseCode) {
      case -1:
        errorMsg = "Connection failed";
        break;
      case -2:
        errorMsg = "Connection lost";
        break;
      case -3:
        errorMsg = "Connection timed out";
        break;
      case -4:
        errorMsg = "Server sent invalid response";
        break;
      case -5:
        errorMsg = "Connection refused";
        break;
      case -6:
        errorMsg = "Invalid server response";
        break;
      case -7:
        errorMsg = "Failed to allocate stream";
        break;
      case -8:
        errorMsg = "Not enough memory";
        break;
      case -9:
        errorMsg = "Invalid HTTP response";
        break;
      case -10:
        errorMsg = "More data pending";
        break;
      case -11:
        errorMsg = "Connection timeout";
        break;
      default:
        errorMsg = "Unknown error";
    }
    Serial.printf("Name request failed, error: %d (%s)\n", httpResponseCode, errorMsg.c_str());
  }
  
  http.end();
  
  return vehicleName;
}

// Function to handle root path on web server
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Watchy Status</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "</head><body><h1>Watchy Status</h1>";
  
  // Battery section
  html += "<h2>Battery: " + String(batteryVoltage, 2) + "V</h2>";
  
  // Vehicles section
  html += "<h2>Vehicles:</h2>";
  if (vehicleCount > 0) {
    html += "<ul>";
    for (int i = 0; i < vehicleCount; i++) {
      html += "<li>" + vehicleNames[i];
      if (vehicleVoltages[i] > 0) {
        html += ": " + String(vehicleVoltages[i], 1) + "V";
      } else {
        html += ": --";
      }
      html += "</li>";
    }
    html += "</ul>";
  } else {
    html += "<p>No vehicles</p>";
  }
  
  // WiFi status
  html += "<p>WiFi: " + String(wifiConnected ? WIFI_SSID : "----") + "</p>";
  
  // IP Address
  html += "<p>IP: " + ipAddress + "</p>";
  
  // Uptime
  html += "<p>Uptime: " + String(deviceUptime) + "m</p>";
  
  // Control buttons
  html += "<p><a href=\"/\">Refresh</a> | <a href=\"/reboot\" onclick=\"return confirm('Are you sure you want to reboot the device?');\">Reboot</a></p>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Handle reboot request
void handleReboot() {
  server.send(200, "text/html", "<html><body><h1>Rebooting...</h1><p>Device will restart in a few seconds.</p><p><a href=\"/\">Back to status page</a></p></body></html>");
  delay(1000); // Give server time to send the response
  ESP.restart();
}

// Function to draw UI on the display
void drawUI() {
  display.setFullWindow();
  display.firstPage();
  do {
    // Set display to white background
    display.fillScreen(GxEPD_WHITE);
    
    // Draw battery voltage at top
    float voltage = BatteryDisplay::getInstance()->getVoltage();
    batteryVoltage = voltage; // Store for web server
    char batteryBuffer[16];
    int voltsInt = (int)voltage;
    int voltsDec = (int)((voltage - voltsInt) * 100);
    snprintf(batteryBuffer, sizeof(batteryBuffer), "%d.%02dV", voltsInt, voltsDec);
    
    // Use monospace font for battery (keeps digits aligned)
    display.setFont(&FreeSansBold18pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(0, 50);
    display.setTextSize(2);
    display.print(batteryBuffer);
    
    // Draw mavlink status - up to 3 vehicles
    int n = MDNS.queryService("mavlink", "udp");
    int yPos = 80; // Start higher up since we removed the "Vehicle" label
    
    // Reset vehicle count for web server
    vehicleCount = 0;
    
    if (n > 0) {
      // Array to store unique IP addresses
      String uniqueIPs[3]; // Store up to 3 unique IPs
      int uniqueCount = 0;
      
      // Find unique IP addresses
      for (int i = 0; i < n && uniqueCount < 3; i++) {
        String currentIP = MDNS.IP(i).toString();
        
        // Filter out invalid IPs like 0.0.0.0 early
        if (currentIP == "0.0.0.0" || currentIP.length() == 0) {
          Serial.println("Skipping invalid IP: " + currentIP);
          continue;
        }
        
        bool isDuplicate = false;
        
        // Check if this IP is already in our unique list
        for (int j = 0; j < uniqueCount; j++) {
          if (currentIP == uniqueIPs[j]) {
            isDuplicate = true;
            break;
          }
        }
        
        // If not a duplicate, add to our list
        if (!isDuplicate) {
          uniqueIPs[uniqueCount++] = currentIP;
          Serial.println("Found unique vehicle IP: " + currentIP);
        }
      }
      
      // Display unique vehicles and fetch their voltages
      // Use proportional font for vehicle names and voltage display
      display.setFont(&FreeMonoBold12pt7b);
      display.setTextSize(1); // Using a larger font but smaller text size for better clarity
      
      for (int i = 0; i < uniqueCount; i++) {
        // Get vehicle name
        String vehicleName = getVehicleName(uniqueIPs[i]);
        
        // Truncate name even more to fit display at larger text size (max 8 chars)
        if (vehicleName.length() > 7) {
          vehicleName = vehicleName.substring(0, 7);
        }
        
        // Get battery voltage from the vehicle
        float batteryVoltage = getMavlinkBatteryVoltage(uniqueIPs[i]);
        
        // Store for web server
        vehicleNames[vehicleCount] = vehicleName;
        vehicleVoltages[vehicleCount] = batteryVoltage;
        vehicleCount++;
        
        // Display the name and battery voltage
        char vehicleBuffer[32];
        if (batteryVoltage > 0) {
          snprintf(vehicleBuffer, sizeof(vehicleBuffer), "%s %.1fV", vehicleName.c_str(), batteryVoltage);
        } else {
          snprintf(vehicleBuffer, sizeof(vehicleBuffer), "%s: --", vehicleName.c_str());
        }
        
        display.setCursor(0, yPos);
        display.print(vehicleBuffer);
        yPos += 35; // Adjusted spacing for proportional font
      }
      
      if (uniqueCount == 0) {
        display.setCursor(4, yPos);
        display.print("No vehicles");
      }
    } else {
      // Use proportional font
      display.setFont(&FreeMonoBold12pt7b);
      display.setTextSize(1);
      display.setCursor(4, yPos);
      display.print("No vehicles");
    }
    
    // Draw WiFi status with icon and SSID
    display.setFont(&FreeSans9pt7b); // Use smaller proportional font for WiFi info
    display.setTextSize(1);
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (wifiConnected) {
      // Draw WiFi icon
      display.drawBitmap(4, 165, WIFI_ICON, 20, 20, GxEPD_BLACK);
      
      // Draw SSID
      display.setCursor(30, 179);
      display.print(WIFI_SSID);
      
      // Draw IP address below SSID
      display.setCursor(30, 197);
      display.print(ipAddress);
    } else {
      display.setCursor(4, 180);
      display.print("WiFi: ----");
    }
    
    // Add uptime in minutes on the lower right corner
    deviceUptime = millis() / 60000; // Convert milliseconds to minutes
    char uptimeBuffer[16];
    snprintf(uptimeBuffer, sizeof(uptimeBuffer), "%lum", deviceUptime);
    
    int16_t tbx, tby;
    uint16_t tbw, tbh;
    display.getTextBounds(uptimeBuffer, 0, 0, &tbx, &tby, &tbw, &tbh);
    display.setCursor(display.width() - tbw - 5, 180); // Position on lower right
    display.print(uptimeBuffer);
    
  } while (display.nextPage());
}

// Function to check WiFi connection and reconnect if needed
bool checkAndReconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    // If this is a new disconnection, record the time
    if (wifiDisconnectedTime == 0) {
      wifiDisconnectedTime = millis();
      reconnectionAttempts = 0;
      Serial.println("WiFi disconnected, starting reconnection attempts...");
    }
    
    // First scan for the WiFi network
    Serial.println("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    Serial.printf("Scan complete, %d networks found\n", n);
    
    bool networkFound = false;
    for (int i = 0; i < n && !networkFound; i++) {
      Serial.printf("  %d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
      if (WiFi.SSID(i) == WIFI_SSID) {
        networkFound = true;
        Serial.printf("Target network '%s' found with signal strength %d dBm\n", WIFI_SSID, WiFi.RSSI(i));
      }
    }
    
    if (!networkFound) {
      Serial.printf("Target network '%s' not found in scan results\n", WIFI_SSID);
      
      // Get current battery voltage
      float voltage = BatteryDisplay::getInstance()->getVoltage();
      char batteryBuffer[16];
      int voltsInt = (int)voltage;
      int voltsDec = (int)((voltage - voltsInt) * 100);
      snprintf(batteryBuffer, sizeof(batteryBuffer), "%d.%02dV", voltsInt, voltsDec);
      
      // Display sleep message on screen with battery voltage
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        
        // Draw battery voltage in the same position as regular UI
        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(0, 50);
        display.setTextSize(2);
        display.print(batteryBuffer);
        
        // Draw sleep message
        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(10, 120);
        display.setTextSize(1);
        display.println("OFF");
        
        display.setCursor(10, 160);
        display.setFont(&FreeSansBold9pt7b);
        display.println("No WiFi found");
        display.setCursor(10, 180);
        display.println("Sleeping for 60s...");
      } while (display.nextPage());
      
      Serial.println("Going to deep sleep for 60 seconds...");
      delay(1000); // Give time for serial output and display to complete
      
      // Configure wake up source as timer
      esp_sleep_enable_timer_wakeup(WIFI_DEEP_SLEEP_DURATION);
      
      // Go to deep sleep
      esp_deep_sleep_start();
      
      // Code will continue from setup() after waking up
      return false; // This line won't actually execute due to deep sleep
    }
    
    // Increment attempt counter
    reconnectionAttempts++;
    
    Serial.printf("WiFi network found, attempt %d of %d to connect...\n", 
                 reconnectionAttempts, MAX_RECONNECTION_ATTEMPTS);
    
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Wait briefly for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      // Reset the disconnection timer and attempt counter since we're connected again
      wifiDisconnectedTime = 0;
      reconnectionAttempts = 0;
      
      ipAddress = WiFi.localIP().toString(); // Update IP address variable
      Serial.println("\nWiFi reconnected");
      Serial.print("IP address: ");
      Serial.println(ipAddress);
      
      // Restart mDNS service after reconnecting
      MDNS.end();
      if (MDNS.begin(OTA_HOSTNAME)) {
        Serial.println("mDNS responder restarted");
      } else {
        Serial.println("Error restarting mDNS responder");
      }
      
      return true; // Return true to indicate successful connection
    } else if (reconnectionAttempts >= MAX_RECONNECTION_ATTEMPTS) {
      // We've tried enough times, go to deep sleep to save power
      Serial.println("Maximum WiFi reconnection attempts reached.");
      
      // Get current battery voltage
      float voltage = BatteryDisplay::getInstance()->getVoltage();
      char batteryBuffer[16];
      int voltsInt = (int)voltage;
      int voltsDec = (int)((voltage - voltsInt) * 100);
      snprintf(batteryBuffer, sizeof(batteryBuffer), "%d.%02dV", voltsInt, voltsDec);
      
      // Display sleep message on screen with battery voltage
      display.setFullWindow();
      display.firstPage();
      do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        
        // Draw battery voltage in the same position as regular UI
        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(0, 50);
        display.setTextSize(2);
        display.print(batteryBuffer);
        
        // Draw sleep message
        display.setFont(&FreeSansBold18pt7b);
        display.setCursor(10, 120);
        display.setTextSize(1);
        display.println("OFF");
        
        display.setCursor(10, 160);
        display.setFont(&FreeSansBold9pt7b);
        display.println("WiFi connect failed");
        display.setCursor(10, 180);
        display.println("Sleeping for 60s...");
      } while (display.nextPage());
      
      Serial.println("Going to deep sleep for 60 seconds to save power...");
      delay(1000); // Give time for serial output and display to complete
      
      // Configure wake up source as timer
      esp_sleep_enable_timer_wakeup(WIFI_DEEP_SLEEP_DURATION);
      
      // Go to deep sleep
      esp_deep_sleep_start();
      
      // Code will continue from setup() after waking up
      return false; // This line won't actually execute due to deep sleep
    }
    
    return false; // Return false to indicate connection failed
  } else {
    // We're connected, reset the disconnection timer and attempt counter
    wifiDisconnectedTime = 0;
    reconnectionAttempts = 0;
    
    // Print IP address 
    ipAddress = WiFi.localIP().toString();
    Serial.print("WiFi connected. IP address: ");
    Serial.println(ipAddress);
    
    return true; // Return true to indicate already connected
  }
}

// Setup WiFi and OTA updates
bool setupWiFiAndOTA() {
  // Try to connect using the common reconnect function
  bool connected = checkAndReconnectWiFi();
  
  // If connection failed, the function will go to deep sleep
  // so we won't reach this point unless we're connected
  if (!connected) {
    return false;
  }
  
  // Set up mDNS responder
  if (MDNS.begin(OTA_HOSTNAME)) {
    Serial.println("mDNS responder started");
    // Explicitly advertise the OTA service
    MDNS.addService("arduino", "tcp", 3232);
    Serial.printf("You can update firmware using: %s.local\n", OTA_HOSTNAME);
  } else {
    Serial.println("Error starting mDNS responder");
  }
  
  // Initialize OTA
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting...");
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
  
  return true;
}

// Function to setup web server
void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/reboot", handleReboot);
  server.begin();
  Serial.println("Web server started");
}

// Arduino setup function
void setup() {
  Serial.begin(115200);
  Serial.println("Starting Watchy without LVGL application");

  // Setup button pins with pullups
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  pinMode(BUTTON_MENU, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  
  // Initialize SPI for the display
  SPI.begin(18, 19, 23, DISPLAY_CS); // SCK, MISO, MOSI, SS
  
  // Initialize the e-paper display
  display.init(115200);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  
  // Initialize battery monitor (do this early to get readings)
  BatteryDisplay::getInstance();
  
  // Set up OTA update functionality using the reconnect function
  // If WiFi is unavailable, this will go to deep sleep
  if (!setupWiFiAndOTA()) {
    // This code will actually never execute because deep sleep will restart the device
    // But we include it for clarity
    Serial.println("Failed to connect to WiFi, going to deep sleep");
    return;
  }
  
  // Setup web server
  setupWebServer();
  
  // Draw initial UI with real data
  drawUI();
  
  Serial.println("Setup complete");
}

// Arduino loop function
void loop() {
  static unsigned long lastDrawTime = 0;
  static unsigned long lastWiFiCheck = 0;
  unsigned long currentTime = millis();

  // Check and reconnect WiFi if disconnected (every 15 seconds instead of 30)
  if (currentTime - lastWiFiCheck >= 15000) {
    checkAndReconnectWiFi();
    lastWiFiCheck = currentTime;
  }

  if (currentTime - lastDrawTime >= 60000 || BatteryDisplay::getInstance()->shouldUpdate()) {
    drawUI();
    lastDrawTime = currentTime;
  }
  
  // Handle OTA updates - moved higher in the loop for priority
  ArduinoOTA.handle();
  
  // Handle web server client requests
  server.handleClient();

  delay(10);  // Reduced from 50ms to 10ms to make the loop more responsive
}