#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the WS2812B LED strip demo
 * 
 * @param gpio_num GPIO pin connected to the data line of the WS2812B LED strip
 * @param led_count Number of LEDs in the strip
 * @return esp_err_t ESP_OK on success, otherwise error
 */
esp_err_t start_led_strip_demo(uint32_t gpio_num, uint16_t led_count);

#ifdef __cplusplus
}
#endif
