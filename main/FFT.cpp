#include "FFT.h"
#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_dsp.h"
#include "led_strip_control.h"
#include "freq_color_mapper.h"
#include "jetson_uart.h"

adc_oneshot_unit_handle_t adc_handle;

// Buffers
float complex_data[2 * FFT_SIZE];
float magnitude_bins[FFT_SIZE / 2];

// Initialize FFT structures
bool initialize_fft() {
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        printf("FFT initialization failed with error: %d\n", ret);
        return false;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1, .ulp_mode = ADC_ULP_MODE_DISABLE};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    return true;
}

// Sample audio from ADC with precise timing
static void sample_audio() {
    int64_t start_time = esp_timer_get_time();
    int sample_period_us = 1000000 / SAMPLE_RATE;

    for (int i = 0; i < FFT_SIZE; ++i) {
        int64_t next_sample_time = start_time + i * sample_period_us;

        int raw_adc = 0;
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_adc);

        complex_data[2 * i] = (float)raw_adc;
        complex_data[2 * i + 1] = 0.0f;

        int64_t now = esp_timer_get_time();
        int64_t delay_us = next_sample_time - now;
        if (delay_us > 0) {
            esp_rom_delay_us(delay_us);
        }
        // int64_t elapsed = esp_timer_get_time() - start_time;
        // printf("Sample time: %lld us\n", elapsed);
    }
}

// Perform FFT and fill magnitude_bins array
static void perform_fft() {
    dsps_fft2r_fc32(complex_data, FFT_SIZE);
    dsps_bit_rev_fc32(complex_data, FFT_SIZE);

    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float real = complex_data[2 * i];
        float imag = complex_data[2 * i + 1];
        magnitude_bins[i] = sqrtf(real * real + imag * imag);
    }
}

void get_dominant_frequency(float* out_freq, float* out_magnitude) {
    int max_index = 1;
    float max_value = magnitude_bins[1];

    for (int i = 2; i < FFT_SIZE / 2; i++) {
        if (magnitude_bins[i] > max_value) {
            max_value = magnitude_bins[i];
            max_index = i;
        }
    }

    *out_freq = (float)max_index * SAMPLE_RATE / FFT_SIZE;
    *out_magnitude = max_value;
}

// void set_brightness(int brightness) {
//     led_strip_set_brightness();
// }

// void set_color() {
//     led_strip_set_pixel_color(0, 0, 0, 0); // Clear previous colors, index r g b
// }
void fft_control_lights() {
    sample_audio();
    perform_fft();

    float freq, mag;
    get_dominant_frequency(&freq, &mag);
    // printf("Dominant Frequency: %.2f Hz, Magnitude: %.2f\n", freq, mag);

    int brightness = (int)(mag / 4095.0f * 255.0f);
    if (brightness > 255) brightness = 255;

    rgb_t color = map_frequency_to_color(freq, mag);
    jetson_send_color(color); // Send color to Jetson
    printf("Color: R:%d G:%d B:%d\n", color.r, color.g, color.b);
    // printf("Brightness: %d\n", brightness);

    for (int i = 0; i < LED_COUNT; i++) {
        led_strip_set_pixel_color(i, color.r, color.g, color.b);
    }
    
    led_strip_set_brightness(brightness);
}