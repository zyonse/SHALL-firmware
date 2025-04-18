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
void init_display(void);

void lv_tick_task(void *arg);
void start_lvgl_app(void *pvParameters);


#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
