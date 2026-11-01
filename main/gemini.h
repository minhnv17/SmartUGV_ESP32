#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// Gemini Live model. API key is private to gemini.c via local secrets.h.

#define GEMINI_MODEL "gemini-3.8-live"

// Requires initialized microphone and Wi-Fi. Owns microphone reads until reset.
esp_err_t gemini_start(void);

// Audio delivery callbacks used by the Live receiver (nonblocking).
void gemini_on_audio(const int16_t *pcm, size_t samples);
void gemini_on_interrupted(void);
