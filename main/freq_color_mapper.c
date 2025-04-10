#include "freq_color_mapper.h"

// Very simple linear color mapping example
rgb_t map_frequency_to_color(float freq, float magnitude) {
    rgb_t color = {0, 0, 0};

    // Map freq to hue
    if (freq < 300) { color.r = 255; }           // red
    else if (freq < 1000) { color.g = 255; }     // green
    else { color.b = 255; }                      // blue

    // Scale brightness
    float scale = magnitude / 4096.0f;
    color.r *= scale;
    color.g *= scale;
    color.b *= scale;

    return color;
}
