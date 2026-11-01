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
