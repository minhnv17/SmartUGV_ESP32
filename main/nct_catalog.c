#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nct_music.h"

bool nct_valid_key(const char *key)
{
    if (!key || !key[0] || strlen(key) >= NCT_KEY_SIZE) return false;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        if (!isalnum(*p)) return false;
    return true;
}

bool nct_valid_stream_url(const char *url)
{
    if (!url || strncmp(url, "https://", 8) || strlen(url) >= NCT_URL_SIZE) return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) if (*p <= 32 || *p == 127) return false;
    const char *host = url + 8;
    const char *end = strchr(host, '/');
    if (!end || memchr(host, '@', end - host) || memchr(host, ':', end - host)) return false;
    size_t len = end - host;
    const char *suffixes[] = {".nct.vn", ".nixcdn.com"};
    for (size_t i = 0; i < 2; ++i) {
        size_t n = strlen(suffixes[i]);
        if (len > n && !strncmp(end - n, suffixes[i], n)) return true;
    }
    return false;
}

static const char *string(cJSON *obj, const char *field)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, field);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static const char *stream_url(cJSON *song)
{
    if (!cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(song, "statusPlay")) ||
        cJSON_GetObjectItemCaseSensitive(song, "statusPlay")->valuedouble != 1) return NULL;
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(song, "streamURL")) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(item, "status");
        cJSON *vip = cJSON_GetObjectItemCaseSensitive(item, "onlyVIP");
        if (cJSON_IsNumber(status) && status->valuedouble == 1 && cJSON_IsFalse(vip) &&
            !strcmp(string(item, "type"), "128") && nct_valid_stream_url(string(item, "stream")))
            return string(item, "stream");
    }
    return NULL;
}

static void copy_utf8(char *output, size_t capacity, const char *input)
{
    size_t used = 0;
    while (*input) {
        unsigned char ch = *input;
        size_t n = ch < 128 ? 1 : (ch & 0xe0) == 0xc0 ? 2 : (ch & 0xf0) == 0xe0 ? 3 : (ch & 0xf8) == 0xf0 ? 4 : 0;
        if (!n || used + n >= capacity) break;
        bool valid = true;
        for (size_t i = 1; i < n; ++i) if (!input[i] || ((unsigned char)input[i] & 0xc0) != 0x80) { valid = false; break; }
        if (!valid) break;
        memcpy(output + used, input, n);
        used += n;
        input += n;
    }
    output[used] = 0;
}

static bool parse_track(cJSON *song, nct_track_t *track)
{
    const char *key = string(song, "key"), *title = string(song, "name");
    if (!nct_valid_key(key) || !title[0]) return false;
    memset(track, 0, sizeof(*track));
    snprintf(track->key, sizeof(track->key), "%s", key);
    copy_utf8(track->title, sizeof(track->title), title);
    copy_utf8(track->artist, sizeof(track->artist), string(song, "artistName"));
    cJSON *duration = cJSON_GetObjectItemCaseSensitive(song, "duration");
    if (cJSON_IsNumber(duration) && duration->valuedouble > 0 && duration->valuedouble < 86400)
        track->duration = duration->valueint;
    track->playable = stream_url(song) != NULL;
    return true;
}

static cJSON *parse_response(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code) || code->valuedouble != 0) { cJSON_Delete(root); return NULL; }
    return root;
}

esp_err_t nct_parse_search(const char *json, nct_track_t *tracks, size_t *count)
{
    if (!json || !tracks || !count || !*count) return ESP_ERR_INVALID_ARG;
    size_t capacity = *count < NCT_MAX_TRACKS ? *count : NCT_MAX_TRACKS;
    *count = 0;
    cJSON *root = parse_response(json);
    cJSON *songs = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "data"), "songs");
    esp_err_t ret = cJSON_IsArray(songs) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    cJSON *song;
    cJSON_ArrayForEach(song, songs) {
        if (*count == capacity) break;
        if (parse_track(song, &tracks[*count])) ++*count;
    }
    cJSON_Delete(root);
    return ret;
}
esp_err_t nct_parse_detail(const char *json, nct_track_t *track, char *url, size_t url_size)
{
    if (!json || !track || !url || !url_size) return ESP_ERR_INVALID_ARG;
    url[0] = 0;
    cJSON *root = parse_response(json);
    cJSON *song = cJSON_GetObjectItemCaseSensitive(root, "data");
    const char *stream = stream_url(song);
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (parse_track(song, track) && stream) {
        if (strlen(stream) < url_size) { strcpy(url, stream); ret = ESP_OK; }
        else ret = ESP_ERR_INVALID_SIZE;
    }
    cJSON_Delete(root);
    return ret;
}
