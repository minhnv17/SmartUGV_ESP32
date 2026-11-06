#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define NCT_MAX_TRACKS 5
#define NCT_KEY_SIZE 33
#define NCT_URL_SIZE 2048
typedef struct {
    char key[NCT_KEY_SIZE];
    char title[128];
    char artist[128];
    unsigned duration;
    bool playable;
} nct_track_t;

bool nct_valid_key(const char *key);
bool nct_valid_stream_url(const char *url);
// Parsers are separate from transport so API fixtures can be tested on a host.
esp_err_t nct_parse_search(const char *json, nct_track_t *tracks, size_t *count);
esp_err_t nct_parse_detail(const char *json, nct_track_t *track, char *url, size_t url_size);
esp_err_t nct_search(const char *query, nct_track_t *tracks, size_t *count);
esp_err_t nct_resolve(const char *key, nct_track_t *track, char *url, size_t url_size);
