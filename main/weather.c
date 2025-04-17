#include "weather.h"
#include "secrets.h"           // Include the secrets header

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <string.h>
#include <ctype.h> // For isalnum
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "weather";

// Define host and path separately
#define WEATHER_API_HOST "api.openweathermap.org"
#define WEATHER_API_PATH "/data/2.5/weather"

// Define buffer size locally (adjust if needed, should be generous enough for location)
#define MAX_LOCATION_LEN 128
#define MAX_DESC_LEN 32 // Max length for weather description cache

// Buffer for HTTP response
#define MAX_HTTP_RECV_BUFFER 1024
static char http_response_buffer[MAX_HTTP_RECV_BUFFER + 1] = {0};
static int http_response_len = 0;
static bool http_request_complete = false;
static bool http_request_success = false;

// --- Cached Weather Data ---
static double cached_temp = -999.0; // Default invalid temp
static int cached_condition_id = -1; // Default invalid ID
static char cached_condition_desc[MAX_DESC_LEN] = "unknown"; // Default description
// --- End Cached Weather Data ---

// Forward declaration for url_encode
static char *url_encode(const char *str, char *encoded_buf, size_t buf_len);

// HTTP Event Handler (static as it's only used within this file)
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            http_request_complete = true;
            http_request_success = false;
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Append data to buffer, checking for overflow
            if (!evt->user_data) break; // Should not happen if buffer provided
            if (http_response_len + evt->data_len < MAX_HTTP_RECV_BUFFER) {
                memcpy(http_response_buffer + http_response_len, evt->data, evt->data_len);
                http_response_len += evt->data_len;
                http_response_buffer[http_response_len] = 0; // Null-terminate
            } else {
                ESP_LOGW(TAG, "HTTP response buffer overflow");
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            http_request_complete = true;
            http_request_success = true; // Assume success if finished without error
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            // Check if disconnection happened before finishing
            if (!http_request_complete) {
                 ESP_LOGI(TAG, "HTTP disconnected before finishing.");
                 http_request_complete = true;
                 http_request_success = false;
            }
            break;
        case HTTP_EVENT_REDIRECT:
             ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
             break;
    }
    return ESP_OK;
}

esp_err_t weather_init(void) {
    ESP_LOGI(TAG, "Weather module initialized.");
    // No longer need to load secrets here
    // Check if macros are defined (optional sanity check)
    #ifndef WEATHER_API_KEY
    #error "WEATHER_API_KEY is not defined in secrets.h"
    #endif
    #ifndef WEATHER_API_LOCATION
    #error "WEATHER_API_LOCATION is not defined in secrets.h"
    #endif
    if (strlen(WEATHER_API_KEY) == 0 || strcmp(WEATHER_API_KEY, "YOUR_ACTUAL_API_KEY") == 0) {
        ESP_LOGW(TAG, "Weather API Key seems to be missing or using placeholder in secrets.h");
    }
     if (strlen(WEATHER_API_LOCATION) == 0 || strcmp(WEATHER_API_LOCATION, "YOUR_ACTUAL_CITY,COUNTRY") == 0) {
        ESP_LOGW(TAG, "Weather API Location seems to be missing or using placeholder in secrets.h");
    }
    return ESP_OK;
}

esp_err_t fetch_and_update_weather_state(void) {
    ESP_LOGI(TAG, "Attempting to fetch weather data and update cache");
    esp_err_t final_err = ESP_FAIL; // Default to failure

    // Check if secrets seem valid (basic check)
    if (strlen(WEATHER_API_KEY) == 0 || strlen(WEATHER_API_LOCATION) == 0 ||
        strcmp(WEATHER_API_KEY, "YOUR_ACTUAL_API_KEY") == 0 ||
        strcmp(WEATHER_API_LOCATION, "YOUR_ACTUAL_CITY,COUNTRY") == 0) {
        ESP_LOGE(TAG, "API Key or Location not configured in secrets.h, cannot fetch weather.");
        return ESP_ERR_INVALID_STATE;
    }

    // Check WiFi connection status
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
         ESP_LOGW(TAG, "WiFi not connected, skipping weather update.");
         return ESP_ERR_WIFI_NOT_CONNECT; // Return specific error
    }

    // URL-encode the location
    char encoded_location[MAX_LOCATION_LEN * 3 + 1]; // Worst case: every char needs encoding (%XX) + null
    url_encode(WEATHER_API_LOCATION, encoded_location, sizeof(encoded_location));
    ESP_LOGD(TAG, "Original Location: %s, Encoded: %s", WEATHER_API_LOCATION, encoded_location);

    // Construct the query string separately using the encoded location
    // Increase buffer size to avoid potential truncation warning/error
    char query_buf[512]; // Increased buffer size significantly
    snprintf(query_buf, sizeof(query_buf), "q=%s&appid=%s&units=metric",
             encoded_location, // Use encoded location
             WEATHER_API_KEY);

    // Reset buffer and flags for new request
    memset(http_response_buffer, 0, sizeof(http_response_buffer));
    http_response_len = 0;
    http_request_complete = false;
    http_request_success = false;

    // Configure HTTP client using host, path, and query
    esp_http_client_config_t config = {
        .host = WEATHER_API_HOST,
        .path = WEATHER_API_PATH,
        .query = query_buf, // Pass the constructed query string
        .transport_type = HTTP_TRANSPORT_OVER_TCP, // Explicitly use TCP (default for http)
        .event_handler = _http_event_handler,
        .user_data = http_response_buffer,
        .disable_auto_redirect = true,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP client (check URL components/encoding?)");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                 status_code,
                 esp_http_client_get_content_length(client));

        if (http_request_success && status_code == 200) {
            ESP_LOGI(TAG, "Weather data received successfully.");
            ESP_LOGD(TAG, "Response: %s", http_response_buffer);

            // Parse JSON
            cJSON *root = cJSON_Parse(http_response_buffer);
            if (root == NULL) {
                ESP_LOGE(TAG, "Error parsing JSON response");
                final_err = ESP_ERR_INVALID_RESPONSE;
            } else {
                // Extract Weather Data
                cJSON *main = cJSON_GetObjectItem(root, "main");
                cJSON *weather_array = cJSON_GetObjectItem(root, "weather");
                cJSON *weather = (cJSON_IsArray(weather_array) && cJSON_GetArraySize(weather_array) > 0) ? cJSON_GetArrayItem(weather_array, 0) : NULL;

                double temp = -999.0;
                int condition_id = -1;
                const char *condition_desc_ptr = "unknown"; // Use a pointer for valuestring

                if (cJSON_IsObject(main)) {
                    cJSON *temp_json = cJSON_GetObjectItem(main, "temp");
                    if (cJSON_IsNumber(temp_json)) {
                        temp = temp_json->valuedouble;
                    }
                }
                if (cJSON_IsObject(weather)) {
                    cJSON *id_json = cJSON_GetObjectItem(weather, "id");
                    cJSON *desc_json = cJSON_GetObjectItem(weather, "main"); // Using "main" description field
                    if (cJSON_IsNumber(id_json)) {
                        condition_id = id_json->valueint;
                    }
                    if (cJSON_IsString(desc_json) && desc_json->valuestring != NULL) {
                        condition_desc_ptr = desc_json->valuestring;
                    }
                }
                ESP_LOGI(TAG, "Parsed Weather: Temp=%.1fC, CondID=%d, Desc=%s", temp, condition_id, condition_desc_ptr);

                // --- Update Cache ---
                cached_temp = temp;
                cached_condition_id = condition_id;
                strncpy(cached_condition_desc, condition_desc_ptr, MAX_DESC_LEN - 1);
                cached_condition_desc[MAX_DESC_LEN - 1] = '\0'; // Ensure null termination
                ESP_LOGI(TAG, "Weather cache updated.");
                // --- End Update Cache ---

                // Mark success if parsing worked
                final_err = ESP_OK;

                cJSON_Delete(root);
            }
        } else {
            ESP_LOGE(TAG, "HTTP GET request failed or returned status %d", status_code);
            final_err = ESP_ERR_HTTP_BASE; // Generic HTTP error
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        final_err = err; // Propagate HTTP client error
    }

    esp_http_client_cleanup(client);
    return final_err;
}

// --- Getter functions for cached data ---
double weather_get_cached_temp(void) {
    return cached_temp;
}

int weather_get_cached_condition_id(void) {
    return cached_condition_id;
}

const char* weather_get_cached_condition_desc(void) {
    return cached_condition_desc;
}
// --- End Getter functions ---

// Simple URL encoder (handles common problematic characters like space)
// Note: This is a basic encoder, might need expansion for more complex cases.
static char *url_encode(const char *str, char *encoded_buf, size_t buf_len) {
    const char *pstr = str;
    char *pbuf = encoded_buf;
    size_t remaining_len = buf_len - 1; // Leave space for null terminator

    while (*pstr && remaining_len > 0) {
        if (isalnum((unsigned char)*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~' || *pstr == ',') {
             // Keep allowed characters as is (added comma)
            if (remaining_len >= 1) {
                *pbuf++ = *pstr;
                remaining_len--;
            } else {
                break; // Not enough space
            }
        } else if (*pstr == ' ') {
            // Encode space as %20
             if (remaining_len >= 3) {
                *pbuf++ = '%';
                *pbuf++ = '2';
                *pbuf++ = '0';
                remaining_len -= 3;
            } else {
                break; // Not enough space
            }
        } else {
            // Encode other characters as %XX
            if (remaining_len >= 3) {
                snprintf(pbuf, 4, "%%%02X", (unsigned char)*pstr); // Use snprintf for safety
                pbuf += 3;
                remaining_len -= 3;
            } else {
                break; // Not enough space
            }
        }
        pstr++;
    }
    *pbuf = '\0'; // Null-terminate
    return encoded_buf;
}
