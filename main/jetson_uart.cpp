#include "jetson_uart.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freq_color_mapper.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
#include <string.h>

static const char *TAG = "UART_TEST";

void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));

    ESP_LOGI(TAG, "UART initialized on TX=%d, RX=%d", UART_TX_PIN, UART_RX_PIN);
}

void uart_send(const char *data) {
    uart_write_bytes(UART_PORT_NUM, data, strlen(data));
}

int uart_receive(char *data, size_t max_len) {
    return uart_read_bytes(UART_PORT_NUM, (uint8_t *)data, max_len, pdMS_TO_TICKS(100));
}

void jetson_send_color(rgb_t color) {
    char uart_msg[64];
    snprintf(uart_msg, sizeof(uart_msg), "COLOR R:%d G:%d B:%d\n", color.r, color.g, color.b);
    uart_send(uart_msg);
}
