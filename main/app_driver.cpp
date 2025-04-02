#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include "bsp/esp-bsp.h"
#include "led_strip_control.h"

#include <app_priv.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

// Forward declaration for attribute update functions
static void app_driver_update_matter_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

/* Do any conversions/remapping for the actual value here */
static esp_err_t app_driver_light_set_power(void *handle, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    bool target_state = val->val.b;
    err = led_strip_set_power(target_state);
    ESP_LOGI(TAG, "LED set power: %d, result: %s", target_state, esp_err_to_name(err));
    return err;
}

static esp_err_t app_driver_light_set_brightness(void *handle, esp_matter_attr_val_t *val)
{
    // Map Matter brightness (0-254) to standard brightness (0-255)
    int value = (val->val.u8 == 0) ? 0 : ((val->val.u8 * 255) / 254);
    ESP_LOGI(TAG, "LED set brightness: %d (Matter value: %d)", value, val->val.u8);
    return led_strip_set_brightness(value);
}

static esp_err_t app_driver_light_set_hue(void *handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
    ESP_LOGI(TAG, "LED set hue: %d", value);
    return led_strip_set_hue(value);
}

static esp_err_t app_driver_light_set_saturation(void *handle, esp_matter_attr_val_t *val)
{
    int value = REMAP_TO_RANGE(val->val.u8, MATTER_SATURATION, STANDARD_SATURATION);
    ESP_LOGI(TAG, "LED set saturation: %d", value);
    return led_strip_set_saturation(value);
}

static esp_err_t app_driver_light_set_temperature(void *handle, esp_matter_attr_val_t *val)
{
    // Matter sends temperature directly in mireds - no conversion needed
    uint32_t mireds = val->val.u16;
    ESP_LOGI(TAG, "LED set temperature: %lu", (unsigned long)mireds);
    return led_strip_set_temperature(mireds);
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    
    // Get current power state from the LED strip
    bool current_state = led_strip_get_power_state();
    bool new_state = !current_state;
    
    // Set the LED strip directly
    esp_err_t err = led_strip_set_power(new_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to toggle LED strip: %s", esp_err_to_name(err));
        return;
    }
    
    // Update the Matter attribute to reflect the new state
    uint16_t endpoint_id = light_endpoint_id;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;
    
    // Only update the attribute if the operation succeeded
    app_driver_update_matter_attribute(endpoint_id, cluster_id, attribute_id);
}

// Helper function to update Matter attributes after hardware changes
static void app_driver_update_matter_attribute(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    
    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        // Read actual state from hardware
        bool power_state = led_strip_get_power_state();
        val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
        val.val.b = power_state;
        ESP_LOGI(TAG, "Updating Matter OnOff attribute to: %d", power_state);
    } else if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        uint8_t brightness = led_strip_get_brightness();
        val.type = ESP_MATTER_VAL_TYPE_UINT8;
        val.val.u8 = REMAP_TO_RANGE(brightness, STANDARD_BRIGHTNESS, MATTER_BRIGHTNESS);
        ESP_LOGI(TAG, "Updating Matter CurrentLevel attribute to: %d", val.val.u8);
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            uint16_t hue = led_strip_get_hue();
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = REMAP_TO_RANGE(hue, STANDARD_HUE, MATTER_HUE);
            ESP_LOGI(TAG, "Updating Matter CurrentHue attribute to: %d", val.val.u8);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            uint8_t saturation = led_strip_get_saturation();
            val.type = ESP_MATTER_VAL_TYPE_UINT8;
            val.val.u8 = REMAP_TO_RANGE(saturation, STANDARD_SATURATION, MATTER_SATURATION);
            ESP_LOGI(TAG, "Updating Matter CurrentSaturation attribute to: %d", val.val.u8);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            uint32_t temperature = led_strip_get_temperature();
            val.type = ESP_MATTER_VAL_TYPE_UINT16;
            val.val.u16 = temperature;
            ESP_LOGI(TAG, "Updating Matter ColorTemperatureMireds attribute to: %d", val.val.u16);
        } else {
            // Unsupported attribute
            return;
        }
    } else {
        // Unsupported cluster/attribute
        return;
    }
    
    // Update the attribute in the Matter stack directly
    attribute_t *attribute = attribute::get(endpoint_id, cluster_id, attribute_id);
    if (attribute) {
        esp_err_t err = attribute::set_val(attribute, &val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set attribute: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "Attribute not found for endpoint:%d cluster:%lu attribute:%lu", 
                endpoint_id, (unsigned long)cluster_id, (unsigned long)attribute_id);
    }
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    
    // Only process if it's for our light endpoint
    if (endpoint_id != light_endpoint_id) {
        return ESP_OK;
    }
    
    void *handle = driver_handle;
    
    // First apply the change to hardware
    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            err = app_driver_light_set_power(handle, val);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            err = app_driver_light_set_brightness(handle, val);
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::ColorMode::Id) {
            if (val->val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
                ESP_LOGI(TAG, "Color mode changed to: HSL (Hue and Saturation)");
            } else if (val->val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
                ESP_LOGI(TAG, "Color mode changed to: Color Temperature");
            } else {
                ESP_LOGI(TAG, "Color mode changed to: %d (unrecognized mode)", val->val.u8);
            }
        } else if (attribute_id == ColorControl::Attributes::EnhancedColorMode::Id) {
            if (val->val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
                ESP_LOGI(TAG, "Enhanced color mode changed to: HSL (Hue and Saturation)");
            } else if (val->val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
                ESP_LOGI(TAG, "Enhanced color mode changed to: Color Temperature");
            } else {
                ESP_LOGI(TAG, "Enhanced color mode changed to: %d (unrecognized mode)", val->val.u8);
            }
        } else if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            err = app_driver_light_set_hue(handle, val);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            err = app_driver_light_set_saturation(handle, val);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            err = app_driver_light_set_temperature(handle, val);
        }
    }
    
    // Check if there's an error in applying hardware change
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error applying attribute change to hardware: %s", esp_err_to_name(err));
        return err;
    }
    
    // After hardware operation succeeds, get the actual state and update val
    if (cluster_id == OnOff::Id && attribute_id == OnOff::Attributes::OnOff::Id) {
        val->val.b = led_strip_get_power_state();
        ESP_LOGI(TAG, "Hardware power state is now: %d", val->val.b);
    } else if (cluster_id == LevelControl::Id && attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
        uint8_t brightness = led_strip_get_brightness();
        val->val.u8 = REMAP_TO_RANGE(brightness, STANDARD_BRIGHTNESS, MATTER_BRIGHTNESS);
        ESP_LOGI(TAG, "Hardware brightness is now: %d (Matter: %d)", brightness, val->val.u8);
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            uint16_t hue = led_strip_get_hue();
            val->val.u8 = REMAP_TO_RANGE(hue, STANDARD_HUE, MATTER_HUE);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            uint8_t saturation = led_strip_get_saturation();
            val->val.u8 = REMAP_TO_RANGE(saturation, STANDARD_SATURATION, MATTER_SATURATION);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            val->val.u16 = led_strip_get_temperature();
        }
    }
    
    return ESP_OK;
}

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_err_t err = ESP_OK;
    void *priv_data = endpoint::get_priv_data(endpoint_id);
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    /* Setting brightness */
    attribute_t *attribute = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_brightness(priv_data, &val);

    /* Setting color */
    attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attribute, &val);
    if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        ESP_LOGI(TAG, "Device using HSL color mode (hue and saturation)");
        /* Setting hue */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        attribute::get_val(attribute, &val);
        err |= app_driver_light_set_hue(priv_data, &val);
        /* Setting saturation */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        attribute::get_val(attribute, &val);
        err |= app_driver_light_set_saturation(priv_data, &val);
    } else if (val.val.u8 == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        ESP_LOGI(TAG, "Device using color temperature mode");
        /* Setting temperature */
        attribute = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attribute, &val);
        err |= app_driver_light_set_temperature(priv_data, &val);
    } else {
        ESP_LOGE(TAG, "Color mode not supported: %d", val.val.u8);
    }

    /* Setting power */
    attribute = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attribute, &val);
    err |= app_driver_light_set_power(priv_data, &val);

    return err;
}

app_driver_handle_t app_driver_light_init()
{
    // Initialize LED strip with GPIO and LED count
    esp_err_t err = led_strip_init(36, LED_COUNT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED strip: %s", esp_err_to_name(err));
    }
    
    // Retry once more after a short delay if failed
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        err = led_strip_init(36, LED_COUNT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Second attempt to initialize LED strip failed: %s", esp_err_to_name(err));
        }
    }
    
    // Return a dummy handle since we're not using it
    void* dummy_handle = (void*)1; // Non-NULL handle
    return (app_driver_handle_t)dummy_handle;
}

app_driver_handle_t app_driver_button_init()
{
    /* Initialize button */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_PRESS_DOWN, app_driver_button_toggle_cb, NULL));
    
    return (app_driver_handle_t)btns[0];
}
