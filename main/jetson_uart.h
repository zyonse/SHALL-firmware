#ifndef JETSON_UART_H
#define JETSON_UART_H

#include "driver/uart.h"
#include "driver/gpio.h"
#include "freq_color_mapper.h"

#define UART_PORT_NUM      UART_NUM_1 // UART1
#define UART_BAUD_RATE     115200
#define UART_TX_PIN        GPIO_NUM_17 // TXD -> Jetson RX (Pin 10)
#define UART_RX_PIN        GPIO_NUM_18 // RXD -> Jetson TX (Pin 8)
#define UART_BUF_SIZE      1024

#ifdef __cplusplus
extern "C" {
#endif

void uart_init(void);
void uart_send(const char *data);
int uart_receive(char *data, size_t max_len);
void jetson_send_color(rgb_t color);

#ifdef __cplusplus
}
#endif

#endif // JETSON_UART_H
