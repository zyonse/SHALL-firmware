#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t start_gpio_toggle(uint32_t gpio_num);

#ifdef __cplusplus
}
#endif
