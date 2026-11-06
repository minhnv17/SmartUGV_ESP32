#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "mic_frontend.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static void tone(int16_t *pcm, size_t count, int amplitude, int offset)
{
    for (size_t i = 0; i < count; ++i)
        pcm[i] = (int16_t)(offset + amplitude * sin(6.283185307179586 * 500 * i / 16000));
}

static void check_weak_then_loud(void)
{
    mic_frontend_t state;
    mic_frontend_init(&state);
    int16_t pcm[320];
    // Same weak input for one second; old fixed 4x produces only ~848 RMS.
    for (unsigned frame = 0; frame < 50; ++frame) {
        tone(pcm, 320, 300, 0);
        mic_frontend_process(&state, pcm, 320);
    }
    mic_frontend_stats_t weak = mic_frontend_take_stats(&state);
    CHECK(weak.raw_rms >= 210 && weak.raw_rms <= 213);
    CHECK(weak.output_rms > 1900);
    CHECK(weak.gain_percent > 1100 && weak.gain_percent <= 1200);
    CHECK(weak.near_full_scale_per_mille == 0);
    // A sudden close voice after weak speech must be protected on frame one.
    tone(pcm, 320, 25000, 0);
    mic_frontend_process(&state, pcm, 320);
    mic_frontend_stats_t loud = mic_frontend_take_stats(&state);
    CHECK(loud.output_peak <= 30000);
    CHECK(loud.raw_peak == 25000 && loud.near_full_scale_per_mille == 0);
    CHECK(loud.gain_percent < 150);
    CHECK(pcm[8] > 0 && pcm[24] < 0); // No sign wraparound.
}

static void check_quiet_and_dc(void)
{
    mic_frontend_t state;
    mic_frontend_init(&state);
    int16_t pcm[320];
    for (unsigned frame = 0; frame < 250; ++frame) {
        tone(pcm, 320, 20, 0);
        mic_frontend_process(&state, pcm, 320);
    }
    mic_frontend_stats_t noise = mic_frontend_take_stats(&state);
    CHECK(noise.gain_percent <= 400); // Near-silence cannot drive gain to 12x.
    CHECK(noise.output_rms > 0);      // No hard gate chopping quiet phonemes.
    CHECK(noise.output_peak < 100);
    mic_frontend_init(&state);
    for (unsigned frame = 0; frame < 100; ++frame) {
        for (unsigned i = 0; i < 320; ++i) pcm[i] = 6000;
        mic_frontend_process(&state, pcm, 320);
    }
    for (unsigned i = 0; i < 320; ++i) CHECK(abs(pcm[i]) <= 1);
    mic_frontend_take_stats(&state);
    for (unsigned i = 0; i < 320; ++i) pcm[i] = 0;
    mic_frontend_process(&state, pcm, 320);
    // The falling edge is a transient, then silence must settle again.
    for (unsigned frame = 0; frame < 10; ++frame) {
        for (unsigned i = 0; i < 320; ++i) pcm[i] = 0;
        mic_frontend_process(&state, pcm, 320);
    }
    CHECK(abs(pcm[319]) <= 1);
}

static void check_edges_and_reset(void)
{
    mic_frontend_t state;
    mic_frontend_init(&state);
    mic_frontend_process(&state, NULL, 0);
    mic_frontend_stats_t empty = mic_frontend_take_stats(&state);
    CHECK(empty.raw_rms == 0 && empty.output_rms == 0 && empty.gain_percent == 400);
    // More than one frame, odd tail, both signed extrema and guards.
    int16_t guarded[643];
    guarded[0] = 1234;
    guarded[642] = -4321;
    for (unsigned i = 1; i < 642; ++i) guarded[i] = i % 2 ? -32768 : 32767;
    mic_frontend_process(&state, guarded + 1, 641);
    CHECK(guarded[0] == 1234 && guarded[642] == -4321);
    for (unsigned i = 1; i < 642; ++i) CHECK(abs(guarded[i]) <= 30000);
    CHECK(state.samples == 641);
    float gain = state.gain, previous = state.previous_output;
    mic_frontend_stats_t clipped = mic_frontend_take_stats(&state);
    CHECK(clipped.raw_peak == 32768 && clipped.near_full_scale_per_mille == 1000);
    CHECK(clipped.output_peak <= 30000);
    CHECK(state.samples == 0 && state.raw_squares == 0 && state.output_squares == 0);
    CHECK(state.gain == gain && state.previous_output == previous);
    mic_frontend_init(&state); // Same reset used after speaker mute/session restart.
    CHECK(state.gain == 4.0f && state.previous_input == 0 && state.previous_output == 0);
    for (unsigned i = 0; i < 641; ++i) guarded[i] = 0;
    mic_frontend_process(&state, guarded, 641);
    for (unsigned i = 0; i < 641; ++i) CHECK(guarded[i] == 0);
}

int main(void)
{
    check_weak_then_loud();
    check_quiet_and_dc();
    check_edges_and_reset();
    puts("Microphone frontend: weak voice, loud transition, noise floor, DC, extrema and reset passed");
    return 0;
}
