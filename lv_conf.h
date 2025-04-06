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

/*--END OF LV_CONF_H--*/
#endif 