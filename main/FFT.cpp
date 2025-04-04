#include "FFT.h"
#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/adc.h"
#include "esp_dsp.h"

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

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);

    return true;
}

// Sample audio from ADC with precise timing
static void sample_audio() {
    int64_t start_time = esp_timer_get_time();
    int sample_period_us = 1000000 / SAMPLE_RATE;

    for (int i = 0; i < FFT_SIZE; ++i) {
        int64_t next_sample_time = start_time + i * sample_period_us;

        int raw_adc = adc1_get_raw(ADC_CHANNEL);
        complex_data[2 * i] = (float)raw_adc;
        complex_data[2 * i + 1] = 0.0f;

        int64_t now = esp_timer_get_time();
        int64_t delay_us = next_sample_time - now;
        if (delay_us > 0) {
            esp_rom_delay_us(delay_us);
        }
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

// Public function: call this from main
void run_fft_cycle() {
    sample_audio();
    perform_fft();
}