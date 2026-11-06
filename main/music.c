#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_ae_rate_cvt.h"
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "music.h"
#include "speaker.h"

#define INPUT_BYTES 4096
#define PCM_BYTES 8192
#define OUTPUT_SAMPLES 8192

static const char *TAG = "MUSIC";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_player;
static nct_track_t s_results[NCT_MAX_TRACKS], s_track;
static size_t s_result_count;
static atomic_uint s_generation;
static atomic_int s_state = MUSIC_IDLE;
static atomic_bool s_wanted, s_paused, s_searching;

typedef struct {
    unsigned generation;
    uint32_t speaker_epoch;
    esp_http_client_handle_t http;
    esp_audio_simple_dec_handle_t decoder;
    esp_ae_rate_cvt_handle_t resampler;
    uint64_t offset, total_bytes;
    char url[NCT_URL_SIZE], location[NCT_URL_SIZE];
    bool range_ok;
    uint8_t input[INPUT_BYTES];
    int16_t pcm[PCM_BYTES / 2], output[OUTPUT_SAMPLES];
    int16_t pending[480];
    size_t pending_count;
    esp_audio_simple_dec_info_t info;
} stream_t;

static bool current(stream_t *s)
{
    return s->generation == atomic_load(&s_generation) && atomic_load(&s_wanted);
}

static esp_err_t stream_event(esp_http_client_event_t *event)
{
    stream_t *s = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_HEADER) {
        if (!strcasecmp(event->header_key, "Location"))
            snprintf(s->location, sizeof(s->location), "%s", event->header_value);
        if (!strcasecmp(event->header_key, "Content-Range")) {
            unsigned long long begin, end, total;
            s->range_ok = sscanf(event->header_value, "bytes %llu-", &begin) == 1 && begin == s->offset;
            if (sscanf(event->header_value, "bytes %llu-%llu/%llu", &begin, &end, &total) == 3)
                s->total_bytes = total;
        }
        if (!s->offset && !strcasecmp(event->header_key, "Content-Length"))
            s->total_bytes = strtoull(event->header_value, NULL, 10);
    }
    return ESP_OK;
}

static esp_err_t open_stream(stream_t *s)
{
    for (int redirect = 0; redirect < 4 && current(s); ++redirect) {
        if (!nct_valid_stream_url(s->url)) return ESP_ERR_INVALID_ARG;
        s->location[0] = 0;
        s->range_ok = false;
        if (!s->offset) s->total_bytes = 0;
        esp_http_client_config_t config = {
            .url = s->url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 3000,
            .buffer_size = 4096, .buffer_size_tx = 1024, .disable_auto_redirect = true,
            .event_handler = stream_event, .user_data = s,
        };
        s->http = esp_http_client_init(&config);
        if (!s->http) return ESP_ERR_NO_MEM;
        if (s->offset) {
            char range[64];
            snprintf(range, sizeof(range), "bytes=%llu-", (unsigned long long)s->offset);
            esp_http_client_set_header(s->http, "Range", range);
        }
        if (esp_http_client_open(s->http, 0) != ESP_OK || esp_http_client_fetch_headers(s->http) < 0) break;
        int code = esp_http_client_get_status_code(s->http);
        if ((!s->offset && code == 200) || (s->offset && code == 206 && s->range_ok)) return ESP_OK;
        if (code < 300 || code > 399 || !nct_valid_stream_url(s->location)) break;
        snprintf(s->url, sizeof(s->url), "%s", s->location);
        esp_http_client_cleanup(s->http);
        s->http = NULL;
    }
    if (s->http) { esp_http_client_cleanup(s->http); s->http = NULL; }
    return ESP_FAIL;
}

// Pausing closes the TCP connection but retains decoder buffers and byte offset.
// Resume requests exactly the next byte with Range, avoiding a restart of the song.
static bool wait_active(stream_t *s)
{
    while (current(s) && atomic_load(&s_paused)) {
        if (s->http) { esp_http_client_cleanup(s->http); s->http = NULL; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!current(s)) return false;
    if (s->total_bytes && s->offset == s->total_bytes) return true;
    if (!s->http && open_stream(s) != ESP_OK) return false;
    return true;
}

static int read_stream(stream_t *s, uint8_t *buffer, size_t size)
{
    if (!wait_active(s)) return -1;
    if (s->total_bytes && s->offset == s->total_bytes) return 0;
    int got = esp_http_client_read(s->http, (char *)buffer, size);
    if (got > 0) s->offset += got;
    if (got == 0 && !esp_http_client_is_complete_data_received(s->http)) return -1;
    return got;
}

static esp_err_t skip_id3(stream_t *s, size_t *retained)
{
    size_t got = 0;
    while (got < 10) {
        int n = read_stream(s, s->input + got, 10 - got);
        if (n <= 0) return ESP_ERR_INVALID_RESPONSE;
        got += n;
    }
    *retained = 10;
    if (memcmp(s->input, "ID3", 3)) return ESP_OK;
    if (s->input[3] < 2 || s->input[3] > 4) return ESP_ERR_NOT_SUPPORTED;
    uint32_t remaining = 0;
    for (int i = 6; i < 10; ++i) {
        if (s->input[i] & 0x80) return ESP_ERR_INVALID_RESPONSE;
        remaining = (remaining << 7) | s->input[i];
    }
    if (s->input[3] == 4 && (s->input[5] & 0x10)) remaining += 10;
    if (remaining > 4 * 1024 * 1024) return ESP_ERR_INVALID_SIZE;
    while (remaining) {
        int n = read_stream(s, s->input, remaining > INPUT_BYTES ? INPUT_BYTES : remaining);
        if (n <= 0) return ESP_ERR_INVALID_RESPONSE;
        remaining -= n;
    }
    *retained = 0;
    return ESP_OK;
}

static esp_err_t flush_pcm(stream_t *s)
{
    while (s->pending_count) {
        if (!wait_active(s) || speaker_music_failed()) return ESP_FAIL;
        if (speaker_music_queue(s->pending, s->pending_count, s->speaker_epoch)) s->pending_count = 0;
    }
    return ESP_OK;
}

static esp_err_t output_pcm(stream_t *s, size_t bytes)
{
    esp_audio_simple_dec_info_t info;
    if (esp_audio_simple_dec_get_info(s->decoder, &info) != ESP_AUDIO_ERR_OK ||
        info.bits_per_sample != 16 || (info.channel != 1 && info.channel != 2) ||
        info.sample_rate < 8000 || info.sample_rate > 48000 || bytes % (2 * info.channel))
        return ESP_ERR_NOT_SUPPORTED;
    if (s->info.sample_rate && (s->info.sample_rate != info.sample_rate || s->info.channel != info.channel))
        return ESP_ERR_NOT_SUPPORTED;
    s->info = info;
    size_t samples = bytes / (2 * info.channel);
    if (info.channel == 2) for (size_t i = 0; i < samples; ++i)
        s->pcm[i] = ((int)s->pcm[2 * i] + s->pcm[2 * i + 1]) / 2;
    int16_t *pcm = s->pcm;
    uint32_t converted = samples;
    if (info.sample_rate != 16000) {
        if (!s->resampler) {
            esp_ae_rate_cvt_cfg_t config = {
                .src_rate = info.sample_rate, .dest_rate = 16000, .channel = 1,
                .bits_per_sample = 16, .complexity = 2, .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
            };
            if (esp_ae_rate_cvt_open(&config, &s->resampler) != ESP_AE_ERR_OK) return ESP_FAIL;
        }
        if (esp_ae_rate_cvt_get_max_out_sample_num(s->resampler, samples, &converted) != ESP_AE_ERR_OK ||
            converted > OUTPUT_SAMPLES) return ESP_ERR_INVALID_SIZE;
        converted = OUTPUT_SAMPLES;
        if (esp_ae_rate_cvt_process(s->resampler, s->pcm, samples, s->output, &converted) != ESP_AE_ERR_OK)
            return ESP_FAIL;
        pcm = s->output;
    }
    // Queue full 30ms blocks, retaining partial output across decode calls.
    for (size_t i = 0; i < converted;) {
        size_t n = converted - i < 480 - s->pending_count ? converted - i : 480 - s->pending_count;
        memcpy(s->pending + s->pending_count, pcm + i, n * sizeof(int16_t));
        s->pending_count += n;
        i += n;
        if (s->pending_count == 480 && flush_pcm(s) != ESP_OK) return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t play_stream(stream_t *s)
{
    nct_track_t resolved;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    resolved = s_track;
    xSemaphoreGive(s_lock);
    esp_err_t ret = nct_resolve(resolved.key, &resolved, s->url, sizeof(s->url));
    if (ret != ESP_OK || !current(s)) return ESP_FAIL;
    if ((ret = open_stream(s)) != ESP_OK) return ret;
    size_t used;
    if ((ret = skip_id3(s, &used)) != ESP_OK) return ret;
    esp_audio_simple_dec_cfg_t config = {.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3};
    if (esp_audio_simple_dec_open(&config, &s->decoder) != ESP_AUDIO_ERR_OK) return ESP_FAIL;
    bool eof = false, decoded_any = false;
    size_t input_offset = 0;
    unsigned stalled = 0;
    for (;;) {
        if (!wait_active(s)) return ESP_FAIL;
        if (!used && !eof) {
            input_offset = 0;
            int n = read_stream(s, s->input, INPUT_BYTES);
            if (n < 0) return ESP_FAIL;
            used = n;
            eof = n == 0;
        }
        if (eof && !used) return decoded_any ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
        esp_audio_simple_dec_raw_t raw = {.buffer = s->input + input_offset, .len = used, .eos = eof};
        esp_audio_simple_dec_out_t frame = {.buffer = (uint8_t *)s->pcm, .len = PCM_BYTES};
        esp_audio_err_t decoded = esp_audio_simple_dec_process(s->decoder, &raw, &frame);
        if (raw.consumed > used || frame.decoded_size > PCM_BYTES || decoded != ESP_AUDIO_ERR_OK)
            return ESP_ERR_INVALID_RESPONSE;
        if (frame.decoded_size) {
            decoded_any = true;
            if ((ret = output_pcm(s, frame.decoded_size)) != ESP_OK) return ret;
        }
        // Preserve the unconsumed input region, as required by the decoder.
        input_offset += raw.consumed;
        used -= raw.consumed;
        if (!raw.consumed && !frame.decoded_size) {
            if (eof) return decoded_any ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            if (++stalled > 8) return ESP_ERR_INVALID_RESPONSE;
        } else stalled = 0;
    }
}

static void player_task(void *arg)
{
    unsigned processed = 0;
    for (;;) {
        if (!atomic_load(&s_wanted) || processed == atomic_load(&s_generation)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            continue;
        }
        stream_t *s = heap_caps_calloc(1, sizeof(stream_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        processed = atomic_load(&s_generation);
        if (!s) {
            atomic_store(&s_state, MUSIC_ERROR);
            atomic_store(&s_wanted, false);
            xSemaphoreGive(s_lock);
            continue;
        }
        if (!atomic_load(&s_wanted)) { xSemaphoreGive(s_lock); free(s); continue; }
        s->generation = processed;
        s->speaker_epoch = speaker_music_reset();
        speaker_music_enable(!atomic_load(&s_paused));
        xSemaphoreGive(s_lock);
        esp_err_t ret = play_stream(s);
        if (ret == ESP_OK && current(s)) ret = flush_pcm(s);
        if (s->http) esp_http_client_cleanup(s->http);
        if (s->decoder) esp_audio_simple_dec_close(s->decoder);
        if (s->resampler) esp_ae_rate_cvt_close(s->resampler);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (current(s) && ret == ESP_OK) speaker_music_end();
        xSemaphoreGive(s_lock);
        if (current(s) && ret == ESP_OK) {
            while (current(s) && (atomic_load(&s_paused) || speaker_music_is_busy())) vTaskDelay(pdMS_TO_TICKS(50));
            if (speaker_music_failed()) ret = ESP_FAIL;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (current(s)) {
            if (ret != ESP_OK) {
                speaker_music_reset();
                atomic_store(&s_state, MUSIC_ERROR);
                ESP_LOGW(TAG, "Music stopped: %s", esp_err_to_name(ret));
            } else atomic_store(&s_state, MUSIC_IDLE);
            speaker_music_enable(false);
            atomic_store(&s_wanted, false);
        }
        xSemaphoreGive(s_lock);
        free(s);
    }
}

esp_err_t music_start(void)
{
    if (s_player) return ESP_ERR_INVALID_STATE;
    if (esp_mp3_dec_register() != ESP_AUDIO_ERR_OK) return ESP_FAIL;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(player_task, "music_player", 8192, NULL, 3, &s_player) != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool music_available(void) { return s_player != NULL; }
bool music_is_searching(void) { return atomic_load(&s_searching); }
music_state_t music_state(void)
{
    music_state_t state = atomic_load(&s_state);
    if (state == MUSIC_BUFFERING && speaker_music_started() && speaker_music_is_busy()) return MUSIC_PLAYING;
    return state;
}
void music_get_track(nct_track_t *track)
{
    if (!track) return;
    memset(track, 0, sizeof(*track));
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *track = s_track;
    xSemaphoreGive(s_lock);
}
esp_err_t music_search(const char *query, nct_track_t *tracks, size_t *count)
{
    if (!music_available()) return ESP_ERR_INVALID_STATE;
    atomic_store(&s_searching, true);
    esp_err_t ret = nct_search(query, tracks, count);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_result_count = ret == ESP_OK ? *count : 0;
    if (s_result_count) memcpy(s_results, tracks, s_result_count * sizeof(nct_track_t));
    xSemaphoreGive(s_lock);
    atomic_store(&s_searching, false);
    return ret;
}
esp_err_t music_play(const char *track_id)
{
    if (!music_available() || !nct_valid_key(track_id)) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (size_t i = 0; i < s_result_count; ++i) {
        if (!strcmp(s_results[i].key, track_id) && s_results[i].playable) {
            s_track = s_results[i];
            atomic_store(&s_paused, false);
            atomic_store(&s_wanted, true);
            speaker_music_reset();
            atomic_store(&s_state, MUSIC_BUFFERING);
            atomic_fetch_add(&s_generation, 1);
            ret = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    if (ret == ESP_OK) xTaskNotifyGive(s_player);
    return ret;
}
esp_err_t music_control(const char *action)
{
    if (!music_available() || !action) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!strcmp(action, "stop")) {
        atomic_store(&s_wanted, false);
        atomic_fetch_add(&s_generation, 1);
        speaker_music_reset();
        atomic_store(&s_paused, false);
        atomic_store(&s_state, MUSIC_IDLE);
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    if (!atomic_load(&s_wanted)) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    if (!strcmp(action, "pause")) {
        atomic_store(&s_paused, true);
        speaker_music_enable(false);
        atomic_store(&s_state, MUSIC_PAUSED);
    } else if (!strcmp(action, "resume")) {
        atomic_store(&s_paused, false);
        speaker_music_enable(true);
        atomic_store(&s_state, MUSIC_BUFFERING);
    } else { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_ARG; }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
esp_err_t music_set_volume(unsigned percent)
{
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    speaker_music_set_volume(percent);
    return ESP_OK;
}

unsigned music_get_volume(void) { return speaker_music_get_volume(); }
