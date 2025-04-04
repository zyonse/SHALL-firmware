#ifndef FFT_AUDIO_H
#define FFT_AUDIO_H

#include <stdbool.h>

// Config
#define SAMPLE_RATE 4000
#define FFT_SIZE 256
#define ADC_CHANNEL ADC1_CHANNEL_3  // GPIO4

#ifdef __cplusplus
extern "C" {
#endif

bool initialize_fft(void);
void run_fft_cycle(void);

#ifdef __cplusplus
}
#endif

#endif // FFT_AUDIO_H
