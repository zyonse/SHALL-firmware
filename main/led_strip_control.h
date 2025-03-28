#pragma once

#include <esp_err.h>

#define LED_COUNT 150
#define LED_BRIGHTNESS 255

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the WS2812B LED strip
 * 
 * @param gpio_num GPIO pin connected to the data line of the WS2812B LED strip
 * @param led_count Number of LEDs in the strip
 * @return esp_err_t ESP_OK on success, otherwise error
 */
esp_err_t led_strip_init(uint32_t gpio_num, uint16_t led_count);

/**
 * @brief Set the power state of the LED strip
 * 
 * @param on true to turn on, false to turn off
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_power(bool on);

/**
 * @brief Get the current power state of the LED strip
 * 
 * @return true if on, false if off
 */
bool led_strip_get_power_state(void);

/**
 * @brief Set the brightness of the LED strip
 * 
 * @param brightness Brightness value (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_brightness(uint8_t brightness);

/**
 * @brief Get the current brightness of the LED strip
 * 
 * @return Current brightness value (0-255)
 */
uint8_t led_strip_get_brightness(void);

/**
 * @brief Set the hue of the LED strip
 * 
 * @param hue Hue value (0-359)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_hue(uint16_t hue);

/**
 * @brief Get the current hue of the LED strip
 * 
 * @return Current hue value (0-359)
 */
uint16_t led_strip_get_hue(void);

/**
 * @brief Set the saturation of the LED strip
 * 
 * @param saturation Saturation value (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_saturation(uint8_t saturation);

/**
 * @brief Get the current saturation of the LED strip
 * 
 * @return Current saturation value (0-255)
 */
uint8_t led_strip_get_saturation(void);

/**
 * @brief Set the color temperature of the LED strip
 * 
 * @param temperature Color temperature in mireds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_temperature(uint32_t temperature);

/**
 * @brief Get the current color temperature of the LED strip
 * 
 * @return Current color temperature in mireds
 */
uint32_t led_strip_get_temperature(void);

#ifdef __cplusplus
}
#endif
