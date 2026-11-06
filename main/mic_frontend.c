#include "mic_frontend.h"

#include <math.h>
#include <string.h>

#define FRAME_SAMPLES 320
#define SAMPLE_RATE 16000.0f
#define BASE_GAIN 4.0f
#define MAX_GAIN 12.0f
#define TARGET_RMS 2800.0f
#define ADAPT_FLOOR_RMS 100.0f
#define OUTPUT_LIMIT 30000.0f

void mic_frontend_init(mic_frontend_t *state)
{
    memset(state, 0, sizeof(*state));
    state->gain = BASE_GAIN;
}

static void process_frame(mic_frontend_t *state, int16_t *pcm, size_t count)
{
    float filtered[FRAME_SAMPLES];
    float squares = 0, peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int raw = pcm[i];
        unsigned magnitude = raw < 0 ? (unsigned)-raw : (unsigned)raw;
        state->raw_squares += (uint64_t)((int64_t)raw * raw);
        if (magnitude > state->raw_peak) state->raw_peak = magnitude;
        if (magnitude >= 32760) ++state->near_full_scale;

        // First-order DC/rumble rejection, about 51 Hz at 16 kHz.
        float value = raw - state->previous_input + 0.98f * state->previous_output;
        state->previous_input = raw;
        state->previous_output = value;
        filtered[i] = value;
        squares += value * value;
        if (fabsf(value) > peak) peak = fabsf(value);
    }
    float rms = sqrtf(squares / count);
    float seconds = count / SAMPLE_RATE;
    float desired;
    float time_constant;
    if (rms >= ADAPT_FLOOR_RMS) {
        desired = TARGET_RMS / rms;
        if (desired > MAX_GAIN) desired = MAX_GAIN;
        if (desired < 0.25f) desired = 0.25f;
        // Reduce on loud speech quickly; raise weak speech over ~250 ms.
        time_constant = desired < state->gain ? 0.04f : 0.25f;
    } else {
        // Do not chase near-silence up to maximum gain. This is a level
        // floor for adaptation, not a speech detector or a noise gate.
        desired = BASE_GAIN;
        time_constant = 2.0f;
    }
    float start_gain = state->gain;
    float end_gain = start_gain + (desired - start_gain) * seconds / (time_constant + seconds);
    // Inspect the whole frame before applying gain, protecting even the first
    // sample of sudden loud speech after a quiet phrase. Never wrap int16.
    float safe_gain = peak > 0 ? OUTPUT_LIMIT / peak : MAX_GAIN;
    if (start_gain > safe_gain) start_gain = safe_gain;
    if (end_gain > safe_gain) end_gain = safe_gain;
    for (size_t i = 0; i < count; ++i) {
        float gain = start_gain + (end_gain - start_gain) * (float)(i + 1) / count;
        float value = filtered[i] * gain;
        if (value > OUTPUT_LIMIT) value = OUTPUT_LIMIT;
        if (value < -OUTPUT_LIMIT) value = -OUTPUT_LIMIT;
        int output = (int)value;
        pcm[i] = (int16_t)output;
        state->output_squares += (uint64_t)((int64_t)output * output);
        unsigned magnitude = output < 0 ? (unsigned)-output : (unsigned)output;
        if (magnitude > state->output_peak) state->output_peak = magnitude;
    }
    state->gain = end_gain;
    state->samples += (uint32_t)count;
}

void mic_frontend_process(mic_frontend_t *state, int16_t *pcm, size_t samples)
{
    while (samples) {
        size_t count = samples > FRAME_SAMPLES ? FRAME_SAMPLES : samples;
        process_frame(state, pcm, count);
        pcm += count;
        samples -= count;
    }
}

mic_frontend_stats_t mic_frontend_take_stats(mic_frontend_t *state)
{
    mic_frontend_stats_t stats = {
        .raw_peak = state->raw_peak,
        .output_peak = state->output_peak,
        .gain_percent = (unsigned)(state->gain * 100.0f),
    };
    if (state->samples) {
        stats.raw_rms = (unsigned)sqrt((double)state->raw_squares / state->samples);
        stats.output_rms = (unsigned)sqrt((double)state->output_squares / state->samples);
        stats.near_full_scale_per_mille = (unsigned)((uint64_t)state->near_full_scale * 1000 / state->samples);
    }
    state->raw_squares = state->output_squares = 0;
    state->samples = state->near_full_scale = 0;
    state->raw_peak = state->output_peak = 0;
    return stats;
}
