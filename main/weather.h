#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the weather module (optional setup).
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t weather_init(void);

/**
 * @brief Fetches current weather data and updates the LED strip environmental state if needed.
 *
 * This function performs the HTTP request, parses the response, and calls
 * led_strip_update_environmental_state().
 *
 * @return esp_err_t ESP_OK on success, or an error code if fetching/parsing failed.
 */
esp_err_t fetch_and_update_weather_state(void);

#ifdef __cplusplus
}
#endif
