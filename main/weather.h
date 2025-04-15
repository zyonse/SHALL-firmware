#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the weather module.
 *
 * @return esp_err_t ESP_OK on success.
 */
esp_err_t weather_init(void);

/**
 * @brief Fetches current weather data and updates the internal cache.
 *
 * This function performs the HTTP request, parses the response, and stores
 * the relevant weather data internally. It does NOT directly update the LED state.
 *
 * @return esp_err_t ESP_OK on success, or an error code if fetching/parsing failed.
 */
esp_err_t fetch_and_update_weather_state(void);

/**
 * @brief Get the cached temperature value.
 *
 * @return double The last successfully fetched temperature, or a default value if never fetched.
 */
double weather_get_cached_temp(void);

/**
 * @brief Get the cached weather condition ID.
 *
 * @return int The last successfully fetched condition ID, or a default value if never fetched.
 */
int weather_get_cached_condition_id(void);

/**
 * @brief Get the cached weather condition description.
 *
 * @return const char* Pointer to the last successfully fetched condition description string,
 *                     or a default string if never fetched. The string is statically allocated.
 */
const char* weather_get_cached_condition_desc(void);

#ifdef __cplusplus
}
#endif
