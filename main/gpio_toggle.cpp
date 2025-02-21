#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "gpio_toggle";

// Task that toggles the provided GPIO pin every second.
static void gpio_toggle_task(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    while (1)
    {
        gpio_set_level((gpio_num_t)gpio_num, 1); // cast gpio_num to gpio_num_t
        ESP_LOGI(TAG, "GPIO %u set HIGH", (unsigned int)gpio_num); // use %u for unsigned value
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level((gpio_num_t)gpio_num, 0);
        ESP_LOGI(TAG, "GPIO %u set LOW", (unsigned int)gpio_num);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern "C" {
    // Public function to start the GPIO toggle task.
    esp_err_t start_gpio_toggle(uint32_t gpio_num)
    {
        // Configure the GPIO pin as output.
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << gpio_num);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure GPIO %u", (unsigned int)gpio_num);
            return ret;
        }

        // Create a FreeRTOS task for toggling the GPIO.
        BaseType_t task_ret = xTaskCreate(gpio_toggle_task, "gpio_toggle_task", 4096, (void *)gpio_num, 5, NULL);
        if (task_ret != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create task for GPIO toggling");
            return ESP_FAIL;
        }
        return ESP_OK;
    }
}
