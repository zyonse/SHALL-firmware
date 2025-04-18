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
 * initializes LVGL, and creates the display driver.
 *
 */
void init_display(void);

/**
 * @brief Displays a static message on the screen once.
 * Assumes init_display() has been called.
 */
void display_static_message(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_H
