#include "led_strip_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include <cmath>

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
static led_strip_mode_t current_mode = MODE_MANUAL; // Default mode

// Convert color temperature to RGB
static void temp2rgb(uint32_t temp_k, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Override for common Matter values (direct mapping approach)
    // Matter uses mireds, where 153 = 6500K (cool) and 370 = 2700K (warm)
    // Instead of complex math, we'll use a direct mapping for key values
    
    uint8_t brightness_factor = current_brightness;
    float brightness_scale = brightness_factor / 255.0f;
    
    // Specific preset colors for common temperatures
    if (temp_k >= 6500) { // 153 mireds or less - Cool white
        *r = 255 * brightness_scale;
        *g = 255 * brightness_scale;
        *b = 255 * brightness_scale;
        ESP_LOGI(TAG, "Using cool white preset (6500K+)");
    } 
    else if (temp_k >= 5000) { // ~200 mireds - Daylight
        *r = 255 * brightness_scale;
        *g = 240 * brightness_scale;
        *b = 230 * brightness_scale;
        ESP_LOGI(TAG, "Using daylight preset (5000-6500K)");
    }
    else if (temp_k >= 4000) { // ~250 mireds - Neutral
        *r = 255 * brightness_scale;
        *g = 225 * brightness_scale;
        *b = 200 * brightness_scale;
        ESP_LOGI(TAG, "Using neutral preset (4000-5000K)");
    }
    else if (temp_k >= 3000) { // ~333 mireds - Warm white
        *r = 255 * brightness_scale;
        *g = 180 * brightness_scale;
        *b = 130 * brightness_scale;
        ESP_LOGI(TAG, "Using warm white preset (3000-4000K)");
    }
    else if (temp_k >= 2700) { // ~370 mireds - Incandescent
        *r = 255 * brightness_scale;
        *g = 160 * brightness_scale;
        *b = 80 * brightness_scale; 
        ESP_LOGI(TAG, "Using incandescent preset (2700-3000K)");
    }
    else { // < 2700K (>370 mireds) - Very warm
        *r = 255 * brightness_scale;
        *g = 140 * brightness_scale;
        *b = 40 * brightness_scale;
        ESP_LOGI(TAG, "Using very warm preset (<2700K)");
    }
    
    ESP_LOGI(TAG, "Temperature %luK -> RGB: (%u,%u,%u) with brightness %u", 
             (unsigned long)temp_k, *r, *g, *b, brightness_factor);
}

// Convert mired to Kelvin
static uint32_t mired_to_kelvin(uint32_t mired)
{
    // Mired is 1,000,000/kelvin
    // To convert: kelvin = 1,000,000/mired
    
    // Handle special case for very large values to avoid division by zero
    if (mired < 1) {
        return 6500; // Default to daylight
    }
    
    // Do the conversion
    uint32_t kelvin = 1000000 / mired;
    
    // Constrain to reasonable values
    if (kelvin < 1000) kelvin = 1000;
    if (kelvin > 10000) kelvin = 10000;
    
    ESP_LOGI(TAG, "Converting %lu mireds to %lu K", (unsigned long)mired, (unsigned long)kelvin);
    
    return kelvin;
}

// Update the LED strip based on current settings
static esp_err_t update_led_strip()
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Updating LED strip - power:%d, mode:%d, brightness:%d",
            power_on, current_mode, current_brightness);

    if (!power_on) {
        // Turn off all LEDs regardless of mode
        ESP_LOGI(TAG, "Turning off all LEDs");
        for (int i = 0; i < strip_led_count; i++) {
            led_strip_set_pixel(led_strip, i, 0, 0, 0);
        }
    } else {
        // Handle different modes only if power is on
        switch (current_mode) {
            case MODE_MANUAL:
                ESP_LOGI(TAG, "Updating in MANUAL mode");
                if (use_temperature_mode) {
                    uint8_t r, g, b;
                    temp2rgb(current_temperature, &r, &g, &b);
                    ESP_LOGI(TAG, "Setting all LEDs to temperature color: RGB(%d,%d,%d)", r, g, b);
                    for (int i = 0; i < strip_led_count; i++) {
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    }
                } else {
                    ESP_LOGI(TAG, "Setting all LEDs to HSV: (%d,%d,%d)",
                            current_hue, current_saturation, current_brightness);
                    for (int i = 0; i < strip_led_count; i++) {
                        led_strip_set_pixel_hsv(led_strip, i, current_hue, current_saturation, current_brightness);
                    }
                }
                break;

            case MODE_ADAPTIVE:
                ESP_LOGI(TAG, "Skipping update in ADAPTIVE mode (handled by FFT task)");
                // Colors are set directly by the FFT algorithm via led_strip_set_pixel_color
                // We only need to refresh the strip, which happens below.
                break;

            case MODE_ENVIRONMENTAL:
                ESP_LOGI(TAG, "Updating in ENVIRONMENTAL mode (placeholder)");
                // TODO: Implement environmental logic based on fetched weather data
                // For now, maybe set a default "environmental" color like blue?
                for (int i = 0; i < strip_led_count; i++) {
                     // Example: Dim blue scaled by brightness
                    uint8_t scaled_blue = (uint8_t)((float)150 * ((float)current_brightness / 255.0f));
                    led_strip_set_pixel(led_strip, i, 0, 0, scaled_blue);
                }
                break;

            default:
                ESP_LOGW(TAG, "Unknown mode: %d", current_mode);
                break;
        }
    }

    // Refresh the strip display unless in adaptive mode (refreshed by FFT task)
    // Allow refresh even if power is off to ensure LEDs are cleared
    if (current_mode != MODE_ADAPTIVE) {
         ESP_LOGI(TAG, "Refreshing LED strip display for mode %d", current_mode);
         return led_strip_refresh(led_strip);
    } else {
        // Adaptive mode refreshes in its own task via led_strip_update()
        return ESP_OK;
    }
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
    
    ESP_LOGI(TAG, "Setting brightness: %d (previous: %d)", brightness, current_brightness);
    current_brightness = brightness;
    
    return update_led_strip();
}

esp_err_t led_strip_set_hue(uint16_t hue)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_hue = hue;
    use_temperature_mode = false; // Setting Hue/Sat implies color mode
    current_mode = MODE_MANUAL;   // Switch back to manual mode
    ESP_LOGI(TAG, "Setting LED strip hue: %d (switched to MANUAL mode)", hue);
    return update_led_strip();
}

esp_err_t led_strip_set_saturation(uint8_t saturation)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    current_saturation = saturation;
    use_temperature_mode = false; // Setting Hue/Sat implies color mode
    current_mode = MODE_MANUAL;   // Switch back to manual mode
    ESP_LOGI(TAG, "Setting LED strip saturation: %d (switched to MANUAL mode)", saturation);
    return update_led_strip();
}

esp_err_t led_strip_set_temperature(uint32_t temperature_mireds)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Setting temperature: %lu mireds (switched to MANUAL mode)", (unsigned long)temperature_mireds);
    
    // Convert mireds to kelvin
    current_temperature = mired_to_kelvin(temperature_mireds);
    
    // Enable temperature mode within MANUAL mode
    use_temperature_mode = true;
    current_mode = MODE_MANUAL; // Switch back to manual mode
    
    // Apply the change
    return update_led_strip();
}

esp_err_t led_strip_set_mode(led_strip_mode_t mode)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (mode < MODE_MANUAL || mode > MODE_ENVIRONMENTAL) {
         ESP_LOGE(TAG, "Invalid mode specified: %d", mode);
         return ESP_ERR_INVALID_ARG;
    }

    current_mode = mode;
    ESP_LOGI(TAG, "Setting LED strip mode: %d", mode);

    // When switching mode, update the strip immediately to reflect the new mode's state
    // (unless switching TO adaptive, which is handled by its task)
    if (current_mode != MODE_ADAPTIVE) {
        return update_led_strip();
    }

    return ESP_OK;
}

led_strip_mode_t led_strip_get_mode(void)
{
    return current_mode;
}

esp_err_t led_strip_set_pixel_color(uint16_t pixel_index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (pixel_index >= strip_led_count) {
        ESP_LOGE(TAG, "Pixel index out of range");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Set pixel even if not in adaptive mode to allow for direct control
    led_strip_set_pixel(led_strip, pixel_index, red, green, blue);
    return ESP_OK;
}

esp_err_t led_strip_refresh(void)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return led_strip_refresh(led_strip);
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
        return 153; // Default to 6500K in mireds
    }
    uint32_t mireds = 1000000 / current_temperature;
    ESP_LOGD(TAG, "Converting %lu K to %lu mireds", 
             (unsigned long)current_temperature, (unsigned long)mireds);
    return mireds;
}

uint16_t led_strip_get_led_count(void)
{
    return strip_led_count;
}

esp_err_t led_strip_update(void)
{
    if (!led_strip) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // This function is primarily called by the adaptive task to refresh
    // Check if still in adaptive mode before refreshing
    if (current_mode == MODE_ADAPTIVE && power_on) {
        ESP_LOGD(TAG, "Refreshing strip from led_strip_update (likely adaptive mode)");
        return led_strip_refresh(led_strip);
    } else {
        ESP_LOGD(TAG, "Skipping refresh in led_strip_update (not adaptive/power off)");
        return ESP_OK; // Don't refresh if not in adaptive mode or powered off
    }
}
