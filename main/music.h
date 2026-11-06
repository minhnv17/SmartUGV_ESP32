#pragma once
#include "nct_music.h"

typedef enum { MUSIC_IDLE, MUSIC_BUFFERING, MUSIC_PLAYING, MUSIC_PAUSED, MUSIC_ERROR } music_state_t;
esp_err_t music_start(void);
bool music_available(void);
esp_err_t music_search(const char *query, nct_track_t *tracks, size_t *count);
esp_err_t music_play(const char *track_id);
esp_err_t music_control(const char *action);
esp_err_t music_set_volume(unsigned percent);
unsigned music_get_volume(void);
music_state_t music_state(void);
bool music_is_searching(void);
// Snapshot, UTF-8 title; suitable for tool responses and logs.
void music_get_track(nct_track_t *track);
