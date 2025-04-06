/**
 * @file app_hal.h
 * @brief Hardware Abstraction Layer (HAL) for ESP32 platform
 */

#ifndef APP_HAL_H
#define APP_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the hardware
 */
void hal_setup(void);

/**
 * Process hardware events and tasks
 */
void hal_loop(void);

/**
 * Cleanup resources before exit
 */
void hal_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_HAL_H */
