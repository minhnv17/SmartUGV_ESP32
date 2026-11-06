#ifndef MIC_FRONTEND_H
#define MIC_FRONTEND_H

#include <stddef.h>
#include <stdint.h>

// Single-owner, 16 kHz mono frontend. No allocation, local VAD or noise gate.
typedef struct {
    float previous_input;
    float previous_output;
    float gain;
    uint64_t raw_squares;
    uint64_t output_squares;
    uint32_t samples;
    uint32_t near_full_scale;
    unsigned raw_peak;
    unsigned output_peak;
} mic_frontend_t;

typedef struct {
    unsigned raw_rms;
    unsigned output_rms;
    unsigned raw_peak;
    unsigned output_peak;
    unsigned near_full_scale_per_mille;
    unsigned gain_percent;
} mic_frontend_stats_t;

void mic_frontend_init(mic_frontend_t *state);
void mic_frontend_process(mic_frontend_t *state, int16_t *pcm, size_t samples);
// Reset the measurement window while preserving filter/gain history.
mic_frontend_stats_t mic_frontend_take_stats(mic_frontend_t *state);

#endif
