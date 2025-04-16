#ifndef DISPLAY_H
#define DISPLAY_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the TFT display hardware and LVGL library.
 *
 * Sets up SPI communication, configures the ILI9341 driver,
 * initializes LVGL, creates the display driver, and sets up
 * the basic screen elements (like the time label).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t init_display(void);

/**
 * @brief Periodic task for display refresh
 *
 * Run from app_main to handle LVGL tasks and update the display.
 *
 * @return ESP_OK on success, or an error code if task creation fails.
 */
esp_err_t update_display(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
