#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "nct_music.h"

#define JSON_LIMIT (48 * 1024)
#define API "https://graph.nhaccuatui.com"

typedef struct { char *buffer; size_t used; bool overflow; } response_t;
static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if ((size_t)event->data_len >= JSON_LIMIT - response->used) {
            response->overflow = true;
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(response->buffer + response->used, event->data, event->data_len);
        response->used += event->data_len;
    }
    return ESP_OK;
}

static char *get_json(const char *url)
{
    response_t response = {.buffer = heap_caps_malloc(JSON_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
    if (!response.buffer) return NULL;
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 8000,
        .buffer_size = 1024, .buffer_size_tx = 1024, .disable_auto_redirect = true,
        .event_handler = http_event, .user_data = &response,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    bool ok = client && esp_http_client_perform(client) == ESP_OK &&
        esp_http_client_get_status_code(client) == 200 && !response.overflow && response.used;
    if (client) esp_http_client_cleanup(client);
    if (!ok) { free(response.buffer); return NULL; }
    response.buffer[response.used] = 0;
    return response.buffer;
}

esp_err_t nct_search(const char *query, nct_track_t *tracks, size_t *count)
{
    if (!query || !query[0] || strlen(query) > 192) return ESP_ERR_INVALID_ARG;
    char url[768];
    size_t used = snprintf(url, sizeof(url), API "/api/v1/search/song?keyword=");
    for (const unsigned char *p = (const unsigned char *)query; *p; ++p) {
        if (isalnum(*p) && *p < 128) url[used++] = *p;
        else used += snprintf(url + used, sizeof(url) - used, "%%%02X", *p);
    }
    snprintf(url + used, sizeof(url) - used, "&pageindex=1&pagesize=5&correct=false");
    char *json = get_json(url);
    if (!json) return ESP_FAIL;
    esp_err_t ret = nct_parse_search(json, tracks, count);
    free(json);
    return ret;
}

esp_err_t nct_resolve(const char *key, nct_track_t *track, char *url, size_t url_size)
{
    if (!nct_valid_key(key)) return ESP_ERR_INVALID_ARG;
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), API "/api/v1/song/detail/%s", key);
    char *json = get_json(endpoint);
    if (!json) return ESP_FAIL;
    esp_err_t ret = nct_parse_detail(json, track, url, url_size);
    if (ret == ESP_OK && strcmp(track->key, key)) { url[0] = 0; ret = ESP_ERR_INVALID_RESPONSE; }
    free(json);
    return ret;
}
