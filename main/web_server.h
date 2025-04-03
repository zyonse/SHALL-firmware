#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the web server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t web_server_init(void);

/**
 * @brief Start the web server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the web server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
