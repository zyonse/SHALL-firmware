#pragma once

#include <esp_err.h>
#include <stdbool.h> // Ensure bool is available
#include <stdint.h>  // Ensure standard integer types are available

#define LED_COUNT 150
#define LED_BRIGHTNESS 255

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enum defining the control modes for the LED strip
 */
typedef enum {
    MODE_MANUAL,        // Controlled by Matter/API (HSV, Temp)
    MODE_ADAPTIVE,      // Controlled by FFT audio analysis
    MODE_ENVIRONMENTAL  // Controlled by external conditions (e.g., weather) - Placeholder
} led_strip_mode_t;

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

/**
 * @brief Set the control mode of the LED strip
 *
 * @param mode The desired control mode (MANUAL, ADAPTIVE, ENVIRONMENTAL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_mode(led_strip_mode_t mode);

/**
 * @brief Get current control mode
 *
 * @return The current led_strip_mode_t
 */
led_strip_mode_t led_strip_get_mode(void);

/**
 * @brief Update the target state for environmental mode based on weather data
 *
 * @param temperature Current temperature (e.g., Celsius)
 * @param condition_id Weather condition code (e.g., from OpenWeatherMap)
 * @param condition_desc Weather condition description (e.g., "Clear", "Rain")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_update_environmental_state(double temperature, int condition_id, const char* condition_desc);

/**
 * @brief Set the color of an individual pixel (for use by FFT algorithm)
 * 
 * @param pixel_index Index of the pixel to set
 * @param red Red component (0-255)
 * @param green Green component (0-255)
 * @param blue Blue component (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_set_pixel_color(uint16_t pixel_index, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Refresh the LED strip to display set colors
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t led_strip_update(void);

/**
 * @brief Get the number of LEDs in the strip
 * 
 * @return uint16_t Number of LEDs
 */
uint16_t led_strip_get_led_count(void);

/**
 * @brief Update the LED strip display based on the current mode and settings.
 *
 * This function reads the current mode, power, brightness, and color settings
 * (including the target environmental color) and applies them to the physical LED strip.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t update_led_strip(void);

#ifdef __cplusplus
}
#endif
