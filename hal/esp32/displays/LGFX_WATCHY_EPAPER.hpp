#ifndef LGFX_WATCHY_EPAPER_HPP
#define LGFX_WATCHY_EPAPER_HPP

/**
 * Display configuration for Watchy e-paper
 * 
 * Specs:
 * - 1.54 inch e-paper display (200x200 pixels)
 * - SPI interface
 * - ESP32 microcontroller
 * - Supports grayscale (not just black and white)
 */

#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// Display Settings for Watchy (200x200 1.54" E-Paper)
#define DISPLAY_WIDTH 200
#define DISPLAY_HEIGHT 200
#define DISPLAY_BITS_PER_PIXEL 1  // 1-bit monochrome for e-paper

// Pin definitions for Watchy display
#define DISPLAY_CS     5
#define DISPLAY_DC     10
#define DISPLAY_RESET  9
#define DISPLAY_BUSY   19

// Button definitions for Watchy
#define BUTTON_MENU    26
#define BUTTON_BACK    25
#define BUTTON_UP      32
#define BUTTON_DOWN    4

// Create the display instance
extern GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;

#endif // LGFX_WATCHY_EPAPER_HPP 