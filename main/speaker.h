#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t speaker_start(void);
// Nonblocking: copies Gemini mono PCM 24kHz into a bounded PSRAM queue.
void speaker_queue_pcm24(const int16_t *pcm, size_t samples);
void speaker_flush(void);
void speaker_end_turn(void);
// Includes DMA drain and acoustic tail. Send silence to Gemini while true.
bool speaker_is_busy(void);
bool speaker_voice_is_busy(void);
void speaker_set_voice_pending(bool pending);
// Music is already decoded/resampled mono PCM16 at 16kHz. The same playback
// task owns I2S for both sources; voice has priority over the music queue.
uint32_t speaker_music_reset(void);
void speaker_music_enable(bool enabled);
void speaker_music_end(void);
bool speaker_music_queue(const int16_t *pcm, size_t samples, uint32_t epoch);
bool speaker_music_is_busy(void);
bool speaker_music_started(void);
bool speaker_music_failed(void);
void speaker_music_set_volume(unsigned percent);
unsigned speaker_music_get_volume(void);
#define SPEAKER_MUSIC_LEVELS 16
typedef struct {
    unsigned volume_percent;
    uint32_t played_samples;
    uint8_t levels[SPEAKER_MUSIC_LEVELS];
} speaker_music_snapshot_t;
void speaker_music_snapshot(speaker_music_snapshot_t *snapshot);
