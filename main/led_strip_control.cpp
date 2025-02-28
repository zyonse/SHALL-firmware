#include "led_strip_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"

static const char *TAG = "led_strip_demo";

// Configuration and state for the LED strip
typedef struct {
    led_strip_handle_t strip;
    uint16_t led_count;
    uint8_t brightness;
} led_strip_state_t;

// Task that demonstrates different LED strip patterns
static void led_strip_task(void *arg)
{
    led_strip_state_t *state = (led_strip_state_t *)arg;
    led_strip_handle_t strip = state->strip;
    uint16_t led_count = state->led_count;
    
    // Animation state
    uint8_t hue = 0;
    
    while (1) {
        // Pattern 1: Rainbow cycle
        for (hue = 0; hue < 255; hue += 5) {
            for (int i = 0; i < led_count; i++) {
                // Calculate color based on position and current hue
                uint32_t offset = (i * 255 / led_count + hue) % 255;
                uint8_t red = 0, green = 0, blue = 0;
                
                // Simple HSV to RGB conversion
                if (offset < 85) {
                    red = 255 - offset * 3;
                    green = offset * 3;
                    blue = 0;
                } else if (offset < 170) {
                    offset -= 85;
                    red = 0;
                    green = 255 - offset * 3;
                    blue = offset * 3;
                } else {
                    offset -= 170;
                    red = offset * 3;
                    green = 0;
                    blue = 255 - offset * 3;
                }
                
                // Apply brightness
                red = red * state->brightness / 255;
                green = green * state->brightness / 255;
                blue = blue * state->brightness / 255;
                
                // Set the LED color
                led_strip_set_pixel(strip, i, red, green, blue);
            }
            
            // Update the strip
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(50)); // Animation speed
        }
        
        // Pattern 2: Flash white three times
        for (int j = 0; j < 3; j++) {
            // Turn all LEDs white
            for (int i = 0; i < led_count; i++) {
                led_strip_set_pixel(strip, i, state->brightness, state->brightness, state->brightness);
            }
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // Turn all LEDs off
            for (int i = 0; i < led_count; i++) {
                led_strip_set_pixel(strip, i, 0, 0, 0);
            }
            led_strip_refresh(strip);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        // Pattern 3: Chase effect
        for (int j = 0; j < 3; j++) {
            // Three moving dots
            for (int step = 0; step < led_count; step++) {
                for (int i = 0; i < led_count; i++) {
                    // Clear all LEDs
                    led_strip_set_pixel(strip, i, 0, 0, 0);
                }
                
                // Set three moving dots with different colors
                led_strip_set_pixel(strip, step % led_count, state->brightness, 0, 0); // Red
                led_strip_set_pixel(strip, (step + led_count/3) % led_count, 0, state->brightness, 0); // Green
                led_strip_set_pixel(strip, (step + 2*led_count/3) % led_count, 0, 0, state->brightness); // Blue
                
                led_strip_refresh(strip);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }
}

extern "C" {
    // Public function to start the LED strip demo
    esp_err_t start_led_strip_demo(uint32_t gpio_num, uint16_t led_count)
    {
        static led_strip_state_t state;
        
        ESP_LOGI(TAG, "Initializing WS2812B LED strip on GPIO %u with %u LEDs",
                (unsigned int)gpio_num, led_count);
        
        // Initialize the RMT peripheral for LED strip control using the new API
        rmt_tx_channel_config_t tx_chan_config = {
            .gpio_num = static_cast<gpio_num_t>(gpio_num),
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000, // 10MHz
            .mem_block_symbols = 64, // 64 * 4 = 256 bits
            .trans_queue_depth = 4,
            .flags = {
                .invert_out = false,
                .with_dma = false,
                .io_loop_back = false
            }
        };
        
        rmt_channel_handle_t led_chan = NULL;
        esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led_chan);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create RMT TX channel: %d", ret);
            return ret;
        }
        
        ret = rmt_enable(led_chan);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to enable RMT channel: %d", ret);
            return ret;
        }
        
        // Install LED strip driver
        led_strip_config_t strip_config = {
            .strip_gpio_num = static_cast<gpio_num_t>(gpio_num),
            .max_leds = led_count,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags = {
                .invert_out = false,
            }
        };
        
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000, // 10MHz
            .flags = {
                .with_dma = false
            }
        };

        led_strip_handle_t led_strip = NULL;
        ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create LED strip object: %d", ret);
            return ret;
        }
        
        // Store state for the task
        state.strip = led_strip;
        state.led_count = led_count;
        state.brightness = 32; // Start with low brightness (0-255)
        
        // Clear all LEDs
        for (int i = 0; i < led_count; i++) {
            led_strip_set_pixel(led_strip, i, 0, 0, 0);
        }
        led_strip_refresh(led_strip);
        
        // Create a FreeRTOS task for LED strip demo
        BaseType_t task_ret = xTaskCreate(led_strip_task, "led_strip_task", 4096, &state, 5, NULL);
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create task for LED strip demo");
            return ESP_FAIL;
        }
        
        return ESP_OK;
    }
}
