#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_rom_sys.h"
#include "esp_dsp.h"
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include "esp_timer.h"

// ADC Configuration
#define ADC_CHANNEL ADC1_CHANNEL_3  // GPIO4
#define SAMPLE_RATE 4000            // Sampling frequency (Hz)
#define FFT_SIZE 256                // Number of samples for FFT

// Buffers
float complex_data[2 * FFT_SIZE];   // Interleaved real and imaginary parts
float magnitude_bins[FFT_SIZE / 2];

// Initialize FFT structures
bool initialize_fft() {
    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        printf("FFT initialization failed with error: %d\n", ret);
        return false;
    }
    return true;
}

// Sample audio from ADC into buffer
void sample_audio() {
    int64_t start_time = esp_timer_get_time();
    int sample_period_us = 1000000 / SAMPLE_RATE;

    for (int i = 0; i < FFT_SIZE; ++i) {
        int64_t next_sample_time = start_time + i * sample_period_us;

        int raw_adc = adc1_get_raw(ADC_CHANNEL);
        complex_data[2 * i] = (float)raw_adc;
        complex_data[2 * i + 1] = 0.0f;

        // Wait until exactly the next sampling instant
        int64_t now = esp_timer_get_time();
        int64_t delay_us = next_sample_time - now;
        if (delay_us > 0) {
            esp_rom_delay_us(delay_us);
        }
    }
}

void perform_fft(float* complex_buffer) {
    dsps_fft2r_fc32(complex_buffer, FFT_SIZE);
    dsps_bit_rev_fc32(complex_buffer, FFT_SIZE);
    // dsps_cplx2reC_fc32(complex_buffer, FFT_SIZE);

    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float real = complex_buffer[2 * i];
        float imag = complex_buffer[2 * i + 1];
        magnitude_bins[i] = sqrtf(real * real + imag * imag);
    }
}

extern "C" void app_main() {
    // Initialize ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11);

    if (!initialize_fft()) {
        printf("FFT initialization error. Halting execution.\n");
        return;
    }

    printf("Starting ESP32-S3 Real-Time FFT Sampling...\n");

    while (true) {
        sample_audio();
        perform_fft(complex_data);

        printf("FFT Magnitude Spectrum:\n");
        for (int i = 28; i <= 36; i++) {
            float frequency = (float)i * SAMPLE_RATE / FFT_SIZE;
            printf("Bin %2d [%5.1f Hz]: Magnitude = %f\n", i, frequency, magnitude_bins[i]);
        }
        printf("\n---\n");

        vTaskDelay(pdMS_TO_TICKS(500));  // Delay between each sampling cycle
    }
}