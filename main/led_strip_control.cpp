#include "led_strip_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include <cmath> // Add for log() and pow() functions

static const char *TAG = "led_strip_control";

// LED strip control variables
static led_strip_handle_t led_strip;
static uint16_t strip_led_count = 0;
static bool power_on = true;
static uint8_t current_brightness = 255;
static uint16_t current_hue = 0;
static uint8_t current_saturation = 255;
static bool use_temperature_mode = false;
static uint32_t current_temperature = 4000; // default is warm white (in kelvin)

// Convert HSV to RGB
static void hsv2rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

// Convert color temperature to RGB
static void temp2rgb(uint32_t temp_k, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Simple approximation: warmer (lower K) = more red, cooler (higher K) = more blue
    float temp = temp_k / 100.0f;
    
    if (temp <= 66) {
        *r = 255;
        float g_val = 99.4708025861f * std::log(temp) - 161.1195681661f;
        *g = g_val < 0 ? 0 : (g_val > 255 ? 255 : static_cast<uint8_t>(g_val));
        
        if (temp <= 19) {
            *b = 0;
        } else {
            float b_val = 138.5177312231f * std::log(temp - 10) - 305.0447927307f;
            *b = b_val < 0 ? 0 : (b_val > 255 ? 255 : static_cast<uint8_t>(b_val));
        }
    } else {
        float r_val = 329.698727446f * std::pow(temp - 60, -0.1332047592f);
        *r = r_val < 0 ? 0 : (r_val > 255 ? 255 : static_cast<uint8_t>(r_val));
        
        float g_val = 288.1221695283f * std::pow(temp - 60, -0.0755148492f);
        *g = g_val < 0 ? 0 : (g_val > 255 ? 255 : static_cast<uint8_t>(g_val));
        
        *b = 255;
    }
}

// Convert mired to Kelvin
static uint32_t mired_to_kelvin(uint32_t mired)
{
    return 1000000 / mired;
}

// Update the LED strip based on current settings
static esp_err_t update_led_strip()
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!power_on) {
        // Turn off all LEDs
        for (int i = 0; i < strip_led_count; i++) {
            led_strip_set_pixel(led_strip, i, 0, 0, 0);
        }
    } else {
        uint8_t r, g, b;
        
        if (use_temperature_mode) {
            temp2rgb(current_temperature, &r, &g, &b);
        } else {
            hsv2rgb(current_hue, current_saturation, 255, &r, &g, &b);
        }
        
        // Apply brightness
        r = (r * current_brightness) / 255;
        g = (g * current_brightness) / 255;
        b = (b * current_brightness) / 255;
        
        // Set all LEDs to the same color
        for (int i = 0; i < strip_led_count; i++) {
            led_strip_set_pixel(led_strip, i, r, g, b);
        }
    }
    
    // Refresh the strip
    return led_strip_refresh(led_strip);
}

esp_err_t led_strip_init(uint32_t gpio_num, uint16_t led_count)
{
    ESP_LOGI(TAG, "Initializing LED strip on GPIO %lu with %u LEDs", (unsigned long)gpio_num, led_count);
    
    // If already initialized, clean up first
    if (led_strip != NULL) {
        led_strip_del(led_strip);
        led_strip = NULL;
    }

    strip_led_count = led_count;
    
    // Configure LED strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = static_cast<int>(gpio_num),
        .max_leds = led_count,
        .led_model = LED_MODEL_WS2812,
    };
    
    // Configure RMT for LED strip
    led_strip_rmt_config_t rmt_config;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz
    rmt_config.flags = {
        .with_dma = false
    };
    
    ESP_LOGI(TAG, "Creating LED strip");
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize with default settings
    power_on = true;
    current_brightness = 64;
    current_hue = 128;
    current_saturation = 254;
    use_temperature_mode = false;
    
    // Update the strip with initial values
    return update_led_strip();
}

esp_err_t led_strip_set_power(bool on)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    power_on = on;
    ESP_LOGI(TAG, "Setting LED strip power: %s", on ? "ON" : "OFF");
    return update_led_strip();
}

esp_err_t led_strip_set_brightness(uint8_t brightness)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_brightness = brightness;
    ESP_LOGI(TAG, "Setting LED strip brightness: %d", brightness);
    return update_led_strip();
}

esp_err_t led_strip_set_hue(uint16_t hue)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_hue = hue;
    use_temperature_mode = false;
    ESP_LOGI(TAG, "Setting LED strip hue: %d", hue);
    return update_led_strip();
}

esp_err_t led_strip_set_saturation(uint8_t saturation)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_saturation = saturation;
    use_temperature_mode = false;
    ESP_LOGI(TAG, "Setting LED strip saturation: %d", saturation);
    return update_led_strip();
}

esp_err_t led_strip_set_temperature(uint32_t temperature_mireds)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Convert mireds to kelvin
    current_temperature = mired_to_kelvin(temperature_mireds);
    use_temperature_mode = true;
    ESP_LOGI(TAG, "Setting LED strip temperature: %lu mireds (%lu K)", 
             (unsigned long)temperature_mireds, (unsigned long)current_temperature);
    return update_led_strip();
}

bool led_strip_get_power_state(void)
{
    return power_on;
}

uint8_t led_strip_get_brightness(void)
{
    return current_brightness;
}

uint16_t led_strip_get_hue(void)
{
    return current_hue;
}

uint8_t led_strip_get_saturation(void)
{
    return current_saturation;
}

uint32_t led_strip_get_temperature(void)
{
    // Convert from kelvin back to mireds
    if (current_temperature == 0) {
        return 0; // Avoid division by zero
    }
    return 1000000 / current_temperature;
}
