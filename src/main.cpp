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
#include "../hal/esp32/displays/LGFX_WATCHY_EPAPER.hpp"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>

// WiFi credentials for OTA updates
#define WIFI_SSID "Pão de Batata"
#define WIFI_PASSWORD "bananaamassadinha"
#define OTA_HOSTNAME "watchy-lvgl"

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
    currentVoltage = voltage;
    Serial.printf("Battery ADC: %d, Voltage: %.2fV\n", adcValue, voltage);
  }
};

// Initialize static instance
BatteryDisplay* BatteryDisplay::instance = nullptr;

// Function to get battery voltage from Mavlink HTTP API
float getMavlinkBatteryVoltage(const String& vehicleIP) {
  float batteryVoltage = -1.0f; // Default value indicating failure
  
  if (vehicleIP.length() == 0 || vehicleIP == "Not found") {
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
    Serial.printf("HTTP request failed, error: %d\n", httpResponseCode);
  }
  
  http.end();
  
  return batteryVoltage;
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
    char batteryBuffer[16];
    int voltsInt = (int)voltage;
    int voltsDec = (int)((voltage - voltsInt) * 100);
    snprintf(batteryBuffer, sizeof(batteryBuffer), "%d.%02dV", voltsInt, voltsDec);
    
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(4, 30);
    display.setTextSize(2);
    display.print(batteryBuffer);
    
    // Draw "Vehicle" label
    display.setTextSize(1);
    display.setCursor(4, 70);
    display.print("Vehicle");
    
    // Draw mavlink status
    char mavlinkBuffer[128];
    bool serviceFound = false;
    String vehicleIP = "";
    
    // Query mDNS for mavlink service
    int n = MDNS.queryService("mavlink", "udp");
    if (n > 0) {
      // Service found - get the IP
      vehicleIP = MDNS.IP(0).toString();
      serviceFound = true;
      
      // Get battery voltage from the vehicle
      float batteryVoltage = getMavlinkBatteryVoltage(vehicleIP);
      
      if (batteryVoltage > 0) {
        // Display IP and battery voltage
        snprintf(mavlinkBuffer, sizeof(mavlinkBuffer), "%s\nBatt: %.2fV", 
                vehicleIP.c_str(), batteryVoltage);
      } else {
        // Could not get battery voltage, just show IP
        snprintf(mavlinkBuffer, sizeof(mavlinkBuffer), "%s\nBatt: --", 
                vehicleIP.c_str());
      }
    } else {
      snprintf(mavlinkBuffer, sizeof(mavlinkBuffer), "Mavlink: Not found");
    }
    
    display.setCursor(4, 100);
    display.print(mavlinkBuffer);
    
    // Draw WiFi status
    char wifiBuffer[64];
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(wifiBuffer, sizeof(wifiBuffer), "WiFi: Connected\nIP: %s", WiFi.localIP().toString().c_str());
    } else {
      snprintf(wifiBuffer, sizeof(wifiBuffer), "WiFi: Disconnected");
    }
    
    display.setCursor(4, 140);
    display.print(wifiBuffer);
    
    // Draw uptime
    display.setCursor(4, 180);
    display.print("Uptime");
    unsigned long uptime = millis() / 1000; // seconds
    unsigned long uptimeMin = uptime / 60;
    unsigned long uptimeHours = uptimeMin / 60;
    display.setCursor(80, 180);
    display.printf("%02lu:%02lu:%02lu", uptimeHours, uptimeMin % 60, uptime % 60);
    
  } while (display.nextPage());
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
  
  // Show a startup message
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(10, 30);
    display.println("Starting...");
    display.setCursor(10, 60);
    display.println("Watchy GxEPD2");
  } while(display.nextPage());
  
  // Allow time for display to refresh
  delay(1000);
  
  // Initialize battery monitor
  BatteryDisplay::getInstance();
  
  // Set up OTA update functionality
  setupOTA();
  
  // Draw initial UI
  drawUI();
  
  Serial.println("Setup complete");
}

// Arduino loop function
void loop() {
  static unsigned long lastDrawTime = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastDrawTime >= 60000 || BatteryDisplay::getInstance()->shouldUpdate()) {
    drawUI();
    lastDrawTime = currentTime;
  }
  
  // Handle OTA updates
  ArduinoOTA.handle();

  delay(50);
}