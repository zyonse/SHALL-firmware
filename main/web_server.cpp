#include "web_server.h"
#include "led_strip_control.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include <string.h> // For strcmp

static const char *TAG = "web_server";
static httpd_handle_t server = NULL;

// Helper function to send JSON response
static esp_err_t send_json_response(httpd_req_t *req, cJSON *root) {
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Helper function to parse request body as JSON
static cJSON* parse_json_request(httpd_req_t *req) {
    int total_len = req->content_len;
    if (total_len <= 0) {
        return NULL;
    }

    char *buffer = (char *)malloc(total_len + 1);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for request body");
        return NULL;
    }

    int cur_len = 0;
    int received = 0;
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buffer + cur_len, total_len - cur_len);
        if (received <= 0) {
            free(buffer);
            return NULL;
        }
        cur_len += received;
    }
    buffer[total_len] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    return root;
}

// API endpoint to get LED strip status
static esp_err_t get_status_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/status");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "power", led_strip_get_power_state());
    cJSON_AddNumberToObject(root, "brightness", led_strip_get_brightness());
    cJSON_AddNumberToObject(root, "hue", led_strip_get_hue());
    cJSON_AddNumberToObject(root, "saturation", led_strip_get_saturation());
    // cJSON_AddBoolToObject(root, "adaptive_mode", led_strip_get_adaptive_mode()); // Removed

    // Add current mode string
    led_strip_mode_t current_mode = led_strip_get_mode();
    const char *mode_str;
    switch (current_mode) {
        case MODE_ADAPTIVE: mode_str = "adaptive"; break;
        case MODE_ENVIRONMENTAL: mode_str = "environmental"; break;
        case MODE_MANUAL:
        default: mode_str = "manual"; break;
    }
    cJSON_AddStringToObject(root, "mode", mode_str);

    return send_json_response(req, root);
}

// API endpoint to control power
static esp_err_t set_power_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/power");
    
    cJSON *root = parse_json_request(req);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *power_json = cJSON_GetObjectItem(root, "power");
    if (!cJSON_IsBool(power_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'power' field");
        return ESP_FAIL;
    }
    
    bool power = cJSON_IsTrue(power_json);
    cJSON_Delete(root);
    
    esp_err_t err = led_strip_set_power(power);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set power");
        return ESP_FAIL;
    }
    
    // Return success response
    root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddBoolToObject(root, "power", led_strip_get_power_state());
    
    return send_json_response(req, root);
}

// API endpoint to control brightness
static esp_err_t set_brightness_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/brightness");
    
    cJSON *root = parse_json_request(req);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *brightness_json = cJSON_GetObjectItem(root, "brightness");
    if (!cJSON_IsNumber(brightness_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'brightness' field");
        return ESP_FAIL;
    }
    
    int brightness = brightness_json->valueint;
    if (brightness < 0 || brightness > 255) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Brightness must be between 0-255");
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    
    esp_err_t err = led_strip_set_brightness((uint8_t)brightness);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set brightness");
        return ESP_FAIL;
    }
    
    // Return success response
    root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "brightness", led_strip_get_brightness());
    
    return send_json_response(req, root);
}

// API endpoint to control color
static esp_err_t set_color_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/color");
    
    cJSON *root = parse_json_request(req);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    cJSON *hue_json = cJSON_GetObjectItem(root, "hue");
    cJSON *saturation_json = cJSON_GetObjectItem(root, "saturation");
    
    if (!cJSON_IsNumber(hue_json) || !cJSON_IsNumber(saturation_json)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'hue' or 'saturation' fields");
        return ESP_FAIL;
    }
    
    int hue = hue_json->valueint;
    int saturation = saturation_json->valueint;
    
    if (hue < 0 || hue > 359 || saturation < 0 || saturation > 255) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid hue (0-359) or saturation (0-255)");
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    
    // Set the hue and saturation
    esp_err_t err = led_strip_set_hue((uint16_t)hue);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set hue");
        return ESP_FAIL;
    }
    
    err = led_strip_set_saturation((uint8_t)saturation);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set saturation");
        return ESP_FAIL;
    }
    
    // Return success response
    root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "hue", led_strip_get_hue());
    cJSON_AddNumberToObject(root, "saturation", led_strip_get_saturation());
    
    return send_json_response(req, root);
}

// API endpoint to control mode
static esp_err_t set_mode_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /api/mode");

    cJSON *root = parse_json_request(req);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *mode_json = cJSON_GetObjectItem(root, "mode");
    if (!cJSON_IsString(mode_json) || (mode_json->valuestring == NULL)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid 'mode' field (must be string)");
        return ESP_FAIL;
    }

    led_strip_mode_t new_mode;
    const char *mode_str = mode_json->valuestring;

    if (strcmp(mode_str, "manual") == 0) {
        new_mode = MODE_MANUAL;
    } else if (strcmp(mode_str, "adaptive") == 0) {
        new_mode = MODE_ADAPTIVE;
    } else if (strcmp(mode_str, "environmental") == 0) {
        new_mode = MODE_ENVIRONMENTAL;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid mode value. Use 'manual', 'adaptive', or 'environmental'.");
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    esp_err_t err = led_strip_set_mode(new_mode);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set mode");
        return ESP_FAIL;
    }

    // Return success response
    root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    // Return the mode string that was actually set
    led_strip_mode_t current_mode = led_strip_get_mode();
    switch (current_mode) {
        case MODE_ADAPTIVE: mode_str = "adaptive"; break;
        case MODE_ENVIRONMENTAL: mode_str = "environmental"; break;
        case MODE_MANUAL:
        default: mode_str = "manual"; break;
    }
    cJSON_AddStringToObject(root, "mode", mode_str);

    return send_json_response(req, root);
}

// API endpoint for CORS preflight requests
static esp_err_t options_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "OPTIONS %s", req->uri);
    
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "3600");
    httpd_resp_send(req, NULL, 0);
    
    return ESP_OK;
}

// Initialize the web server
esp_err_t web_server_init(void) {
    ESP_LOGI(TAG, "Initializing web server");
    return ESP_OK;
}

// Start the web server
esp_err_t web_server_start(void) {
    if (server != NULL) {
        ESP_LOGI(TAG, "Web server already started");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    
    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return ESP_FAIL;
    }
    
    // Define API endpoints
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_uri);
    
    httpd_uri_t power_uri = {
        .uri = "/api/power",
        .method = HTTP_POST,
        .handler = set_power_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &power_uri);
    
    httpd_uri_t brightness_uri = {
        .uri = "/api/brightness",
        .method = HTTP_POST,
        .handler = set_brightness_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &brightness_uri);
    
    httpd_uri_t color_uri = {
        .uri = "/api/color",
        .method = HTTP_POST,
        .handler = set_color_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &color_uri);

    // Add new mode endpoint
    httpd_uri_t mode_uri = {
        .uri = "/api/mode",
        .method = HTTP_POST,
        .handler = set_mode_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &mode_uri);

    // CORS options handler for each endpoint
    httpd_uri_t options_uri_status = {
        .uri = "/api/status",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri_status);
    
    httpd_uri_t options_uri_power = {
        .uri = "/api/power",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri_power);
    
    httpd_uri_t options_uri_brightness = {
        .uri = "/api/brightness",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri_brightness);
    
    httpd_uri_t options_uri_color = {
        .uri = "/api/color",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri_color);

    // Add OPTIONS handler for mode endpoint
    httpd_uri_t options_uri_mode = {
        .uri = "/api/mode",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &options_uri_mode);
    
    ESP_LOGI(TAG, "Web server started successfully");
    return ESP_OK;
}

// Stop the web server
esp_err_t web_server_stop(void) {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    return ESP_OK;
}
