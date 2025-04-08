#pragma once

#include <stdint.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

// Map frequency (Hz) and magnitude to color + brightness
rgb_t map_frequency_to_color(float freq, float magnitude);