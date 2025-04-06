# LVGL for Watchy E-Paper Display

This project demonstrates how to use LVGL (Light and Versatile Graphics Library) with the Watchy v1 e-paper display.

## Hardware

- [Watchy v1](https://watchy.sqfmi.com/) with ESP32 microcontroller
- 1.54-inch e-paper display (200x200 pixels)
- 4 physical buttons for navigation

## Configuration

This project is configured using PlatformIO. The main components are:

- LVGL 9.1 for the UI framework
- GxEPD2 library for e-paper display control
- Custom button navigation for the Watchy hardware

## Building and Flashing

1. Ensure you have PlatformIO installed
2. Clone this repository
3. Connect your Watchy to your computer via USB
4. Build and upload the project:

```
pio run -t upload
```

## Features

- Adapted LVGL to work on monochrome e-paper display
- Custom button navigation (UP, DOWN, ENTER, BACK)
- Optimized refresh rate to work with e-paper's limitations
- Simple UI example with navigable buttons

## Customization

To create your own UI:
1. Modify `src/main.cpp` to create your LVGL interface
2. Use button navigation with `lv_group_t` for a better user experience
3. Keep in mind e-paper display limitations:
   - Slow refresh rate (2 seconds minimum between refreshes)
   - Monochrome display (black and white only)
   - Potential ghosting with frequent updates

## Pin Configuration

The following pins are used:
- Display: CS=5, DC=10, RESET=9, BUSY=19 (SPI interface)
- Buttons: MENU=26, BACK=25, UP=32, DOWN=4

## Resources

- [LVGL Documentation](https://docs.lvgl.io/)
- [GxEPD2 Library](https://github.com/ZinggJM/GxEPD2)
- [Watchy Documentation](https://watchy.sqfmi.com/docs/getting-started)
