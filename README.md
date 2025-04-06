# Watchy with GxEPD2

This project implements a simple and efficient firmware for Watchy e-paper smartwatch using direct GxEPD2 drawing instead of the LVGL library. This simplifies the codebase, reduces memory usage, and improves performance.

## Features

- Direct GxEPD2 drawing for e-paper display
- Battery voltage monitoring and display
- WiFi connectivity and OTA updates
- Mavlink vehicle connection detection via mDNS
- Remote battery monitoring via Mavlink API

## Hardware

- Watchy with ESP32
- 1.54" 200x200 e-paper display (GxEPD2_154_D67)
- 4 buttons for navigation

## Building and Flashing

This project uses PlatformIO for build management. To build and flash:

```bash
# Build the project
pio run

# Upload via serial
pio run -t upload

# Upload via OTA (after initial setup)
pio run -e esp32_watchy_ota -t upload
```

## Development Notes

This project was migrated from an LVGL-based implementation to direct GxEPD2 drawing. This offers several advantages:

1. Simpler codebase with direct control over drawing operations
2. Lower memory usage without the LVGL overhead
3. Better performance on the constrained ESP32 platform
4. More straightforward integration with the e-paper display

## Configuration

The main configuration parameters are located in the main.cpp file:

- WiFi credentials for OTA updates
- Display and button pin assignments
- Battery monitoring parameters

## License

See the LICENSE file for details.
