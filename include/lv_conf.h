/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.1
 * Specifically for Watchy e-paper display
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/*Color depth: 8 (8 bits per pixel for grayscale)*/
#define LV_COLOR_DEPTH 8

/* Grayscale format for e-paper display */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_A8

/*====================
   MEMORY SETTINGS
 *====================*/

/*1: use custom malloc/free*/
#define LV_MEM_CUSTOM 0
#if LV_MEM_CUSTOM == 0
    /*Size of the memory available for `lv_malloc()` in bytes (>= 2kB)*/
    #define LV_MEM_SIZE (48U * 1024U)          /*[bytes]*/
#endif

/*====================
   HAL SETTINGS
 *====================*/

/*Default display refresh period. LVG will redraw changed areas with this period time*/
#define LV_DISPLAY_DEF_REFR_PERIOD 30      /*[ms]*/

/*Input device read period in milliseconds*/
#define LV_INDEV_DEF_READ_PERIOD 30     /*[ms]*/

/*====================
 * FEATURE CONFIGURATION
 *====================*/

/*Enable the mono theme (for e-paper displays)*/
#define LV_USE_THEME_MONO 1

/*-----------
 * Advanced drawing features
 *----------*/
/* Disable all advanced drawing features which cause assembly errors */
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_DRAW_SW_HELIUM 0
#define LV_USE_DRAW_DAVE2D 0
#define LV_USE_DRAW_PXP 0
#define LV_USE_DRAW_VGLITE 0

/*-----------
 * Logging
 *----------*/
#define LV_USE_LOG 1
#if LV_USE_LOG
    /*How important log should be added:
    *LV_LOG_LEVEL_TRACE       A lot of logs to give detailed information
    *LV_LOG_LEVEL_INFO        Log important events
    *LV_LOG_LEVEL_WARN        Log if something unwanted happened but didn't cause a problem
    *LV_LOG_LEVEL_ERROR       Only critical issue, when the system may fail
    *LV_LOG_LEVEL_USER        Only logs added by the user
    *LV_LOG_LEVEL_NONE        Do not log anything*/
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif

/*==================
 * EXAMPLES
 *==================*/

/*Enable the examples to be built with the library*/
#define LV_BUILD_EXAMPLES 0

/*-----------
 * Built-in formats
 *----------*/
#define LV_USE_ALPHA_IMAGE 1
#define LV_USE_GRAYSCALE_IMAGE 1

/*-----------
 * Fonts
 *----------*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1  /* Already in use */
#define LV_FONT_MONTSERRAT_16 1  /* For Uptime label */
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1  /* For Spool and Vehicle labels */
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/*--END OF LV_CONF_H--*/
#endif 