#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "gemini.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy main/secrets.example.h to main/secrets.h and configure GEMINI_API_KEY"
#endif
#include "mic.h"
#include "speaker.h"

#define RX_LIMIT (128 * 1024)
#define CHUNK_SAMPLES 320
#define CONNECTED BIT0
#define READY BIT1
#define RESTART BIT2

static const char *TAG = "GEMINI_LIVE";
// EMBED_TXTFILES appends a NUL byte; keep the UTF-8 prompt in one editable file.
extern const char system_prompt_start[] asm("_binary_system_promt_txt_start");
extern const char system_prompt_end[] asm("_binary_system_promt_txt_end");
static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static char *s_rx;
static unsigned char *s_audio;
static size_t s_used;
static bool s_message;
static char s_resume[2048];
static size_t s_audio_bytes;
static atomic_bool s_model_speaking;

void gemini_on_audio(const int16_t *pcm, size_t samples) { speaker_queue_pcm24(pcm, samples); }
void gemini_on_interrupted(void) { speaker_flush(); atomic_store(&s_model_speaking, false); }

static void restart_session(void)
{
    speaker_flush();
    atomic_store(&s_model_speaking, false);
    xEventGroupClearBits(s_events, READY);
    xEventGroupSetBits(s_events, RESTART);
}

static void transcript(cJSON *content, const char *field, const char *label)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(content, field);
    cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
    if (cJSON_IsString(text) && text->valuestring[0]) {
        // Each line is a streaming fragment, not necessarily a complete sentence.
        printf("%s: %s\n", label, text->valuestring);
    }
}

static void receive_message(void)
{
    cJSON *root = cJSON_Parse(s_rx);
    if (!root) {
        ESP_LOGE(TAG, "Invalid Live JSON; reconnecting");
        restart_session();
        return;
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "setupComplete")) {
        xEventGroupSetBits(s_events, READY);
        ESP_LOGI(TAG, "LIVE SAN SANG: noi truc tiep gan mic, khong can bam BOOT");
        ESP_LOGI(TAG, "Mic dang duoc gui lien tuc toi Gemini; tra loi qua loa va serial; cho loa phat xong roi noi tiep");
    }
    cJSON *resume = cJSON_GetObjectItemCaseSensitive(root, "sessionResumptionUpdate");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(resume, "newHandle");
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(resume, "resumable")) &&
        cJSON_IsString(handle) && strlen(handle->valuestring) < sizeof(s_resume)) {
        strcpy(s_resume, handle->valuestring);
    }
    if (cJSON_GetObjectItemCaseSensitive(root, "goAway")) {
        ESP_LOGI(TAG, "Server goAway; reconnecting with session resumption");
        restart_session();
    }
    cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (error) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(error, "code");
        ESP_LOGE(TAG, "Live API error code=%d", cJSON_IsNumber(code) ? code->valueint : -1);
        restart_session();
    }
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "serverContent");
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(content, "interrupted"))) {
        gemini_on_interrupted();
        s_audio_bytes = 0;
        ESP_LOGI(TAG, "Nguoi dung ngat loi Gemini");
    }
    transcript(content, "inputTranscription", "Ban noi");
    transcript(content, "outputTranscription", "Gemini");
    cJSON *turn = cJSON_GetObjectItemCaseSensitive(content, "modelTurn");
    cJSON *part;
    cJSON_ArrayForEach(part, cJSON_GetObjectItemCaseSensitive(turn, "parts")) {
        cJSON *blob = cJSON_GetObjectItemCaseSensitive(part, "inlineData");
        cJSON *mime = cJSON_GetObjectItemCaseSensitive(blob, "mimeType");
        cJSON *data = cJSON_GetObjectItemCaseSensitive(blob, "data");
        if (!cJSON_IsString(mime) || !cJSON_IsString(data)) continue;
        if (strcmp(mime->valuestring, "audio/pcm;rate=24000") != 0) {
            ESP_LOGW(TAG, "Unsupported audio format");
            continue;
        }
        size_t bytes = 0;
        if (mbedtls_base64_decode(s_audio, RX_LIMIT, &bytes,
                (const unsigned char *)data->valuestring, strlen(data->valuestring)) != 0 || bytes % 2) {
            ESP_LOGW(TAG, "Invalid PCM audio chunk");
            continue;
        }
        s_audio_bytes += bytes;
        atomic_store(&s_model_speaking, true);
        gemini_on_audio((const int16_t *)s_audio, bytes / 2);
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(content, "turnComplete"))) {
        speaker_end_turn();
        atomic_store(&s_model_speaking, false);
        ESP_LOGI(TAG, "Xong luot Gemini, audio nhan=%u bytes (24kHz); tiep tuc nghe", (unsigned)s_audio_bytes);
        s_audio_bytes = 0;
    }
    cJSON_Delete(root);
}

static void websocket_event(void *arg, esp_event_base_t base, int32_t id, void *event)
{
    esp_websocket_event_data_t *data = event;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        s_used = 0;
        s_message = false;
        xEventGroupSetBits(s_events, CONNECTED);
    } else if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED || id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "WebSocket event=%ld; reconnecting", (long)id);
        restart_session();
    } else if (id == WEBSOCKET_EVENT_DATA) {
        if (data->op_code == 0x08) {
            // Do not log close payloads or authentication headers.
            if (data->data_len >= 2) {
                unsigned code = ((unsigned char)data->data_ptr[0] << 8) | (unsigned char)data->data_ptr[1];
                ESP_LOGW(TAG, "WebSocket close code=%u", code);
                if (code == 1008) s_resume[0] = '\0';
            }
            restart_session();
            return;
        }
        if (data->op_code != 0 && data->op_code != 1 && data->op_code != 2) return;
        // payload_offset is per frame. A logical JSON message can span both
        // multiple DATA events and continuation frames (opcode 0).
        if (data->payload_offset == 0 && data->op_code != 0) {
            s_used = 0;
            s_message = true;
        }
        if (!s_message || data->data_len < 0 || (size_t)data->data_len >= RX_LIMIT - s_used) {
            restart_session();
            return;
        }
        if (data->data_len) memcpy(s_rx + s_used, data->data_ptr, data->data_len);
        s_used += data->data_len;
        if (data->fin && data->payload_offset + data->data_len == data->payload_len) {
            s_rx[s_used] = '\0';
            receive_message();
            s_used = 0;
            s_message = false;
        }
    }
}

static bool send_setup(esp_websocket_client_handle_t client)
{
    cJSON *setup = cJSON_Parse(
        "{\"setup\":{\"model\":\"models/" GEMINI_MODEL "\","
        "\"generationConfig\":{\"responseModalities\":[\"AUDIO\"]},"
        "\"systemInstruction\":{\"parts\":[{\"text\":\"\"}]},"
        "\"inputAudioTranscription\":{},\"outputAudioTranscription\":{},"
        "\"realtimeInputConfig\":{\"automaticActivityDetection\":{"
        "\"disabled\":false,\"prefixPaddingMs\":200,\"silenceDurationMs\":600}},"
        "\"sessionResumption\":{},\"contextWindowCompression\":{\"slidingWindow\":{}}}}");
    if (!setup) return false;
    cJSON *instruction = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(setup, "setup"),
            "systemInstruction"), "parts"), 0);
    cJSON *prompt = cJSON_CreateStringReference(system_prompt_start);
    if (!prompt || !cJSON_ReplaceItemInObjectCaseSensitive(instruction, "text", prompt)) {
        cJSON_Delete(prompt);
        cJSON_Delete(setup);
        return false;
    }
    cJSON *resumption = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(setup, "setup"), "sessionResumption");
    if (s_resume[0] && !cJSON_AddStringToObject(resumption, "handle", s_resume)) {
        cJSON_Delete(setup);
        return false;
    }
    char *json = cJSON_PrintUnformatted(setup);
    cJSON_Delete(setup);
    if (!json) return false;
    int length = strlen(json);
    bool ok = esp_websocket_client_send_text(client, json, length, pdMS_TO_TICKS(2000)) == length;
    cJSON_free(json);
    return ok;
}

static void live_task(void *arg)
{
    int16_t pcm[CHUNK_SAMPLES];
    unsigned char encoded[4 * ((sizeof(pcm) + 2) / 3) + 1];
    char message[sizeof(encoded) + 128];
    char headers[256];
    snprintf(headers, sizeof(headers), "x-goog-api-key: %s\r\n", GEMINI_API_KEY);
    unsigned backoff = 1;
    for (;;) {
        xEventGroupClearBits(s_events, CONNECTED | READY | RESTART);
        s_audio_bytes = 0;
        esp_websocket_client_config_t config = {
            .uri = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent",
            .headers = headers,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .disable_auto_reconnect = true,
            .buffer_size = 4096,
            .task_stack = 16384,
            .network_timeout_ms = 10000,
        };
        esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
        bool started = false;
        if (client && esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event, NULL) == ESP_OK &&
            esp_websocket_client_start(client) == ESP_OK) started = true;
        bool setup_sent = false;
        TickType_t began = xTaskGetTickCount();
        if (started) ESP_LOGI(TAG, "Dang mo Gemini Live (%s)", GEMINI_MODEL);
        while (started) {
            size_t count = 0;
            esp_err_t ret = mic_read_pcm16(pcm, CHUNK_SAMPLES, &count, 100);
            EventBits_t bits = xEventGroupGetBits(s_events);
            if (bits & RESTART) break;
            if (!(bits & READY) && xTaskGetTickCount() - began > pdMS_TO_TICKS(30000)) {
                ESP_LOGW(TAG, "Live setup timeout");
                break;
            }
            if ((bits & CONNECTED) && !setup_sent) {
                setup_sent = send_setup(client);
                if (!setup_sent) break;
            }
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Mic read: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (!(bits & READY) || count == 0) continue;
            backoff = 1;
            // Live VAD needs a useful speech level. Fixed gain preserves timing;
            // saturate before casting to avoid signed 16-bit wraparound.
            for (size_t i = 0; i < count; ++i) {
                int value = (int)pcm[i] * 4;
                if (value > INT16_MAX) value = INT16_MAX;
                if (value < INT16_MIN) value = INT16_MIN;
                pcm[i] = (int16_t)value;
            }
            // Half duplex prevents the speaker from triggering server VAD.
            if (atomic_load(&s_model_speaking) || speaker_is_busy()) memset(pcm, 0, count * sizeof(int16_t));
            size_t length = 0;
            if (mbedtls_base64_encode(encoded, sizeof(encoded), &length, (unsigned char *)pcm, count * 2) != 0) break;
            encoded[length] = 0;
            int size = snprintf(message, sizeof(message),
                "{\"realtimeInput\":{\"audio\":{\"mimeType\":\"audio/pcm;rate=16000\",\"data\":\"%s\"}}}", encoded);
            if (size < 0 || size >= sizeof(message) ||
                esp_websocket_client_send_text(client, message, size, pdMS_TO_TICKS(500)) != size) {
                ESP_LOGW(TAG, "Audio send failed; reconnecting to avoid stale audio");
                break;
            }
        }
        xEventGroupClearBits(s_events, READY);
        if (client) {
            if (started) esp_websocket_client_stop(client);
            esp_websocket_client_destroy(client);
        }
        gemini_on_interrupted();
        ESP_LOGW(TAG, "Thu lai sau %u giay; resume %s", backoff, s_resume[0] ? "available" : "unavailable (new context)");
        TickType_t until = xTaskGetTickCount();
        while (xTaskGetTickCount() - until < pdMS_TO_TICKS(backoff * 1000)) {
            size_t count;
            if (mic_read_pcm16(pcm, CHUNK_SAMPLES, &count, 100) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (backoff < 30) backoff = backoff * 2 > 30 ? 30 : backoff * 2;
    }
}

esp_err_t gemini_start(void)
{
    if (s_task) return ESP_ERR_INVALID_STATE;
    if (system_prompt_end - system_prompt_start <= 1 || !system_prompt_start[0]) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "System prompt: system_promt.txt (%u bytes)",
             (unsigned)(system_prompt_end - system_prompt_start - 1));
    if (!GEMINI_API_KEY[0] || strcmp(GEMINI_API_KEY, "YOUR_GEMINI_API_KEY_HERE") == 0 || strlen(GEMINI_API_KEY) > 200) return ESP_ERR_INVALID_ARG;
    s_events = xEventGroupCreate();
    s_rx = heap_caps_malloc(RX_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_audio = heap_caps_malloc(RX_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_events && s_rx && s_audio && xTaskCreate(live_task, "gemini_live", 12288, NULL, 4, &s_task) == pdPASS) return ESP_OK;
    if (s_events) vEventGroupDelete(s_events);
    free(s_rx);
    free(s_audio);
    s_events = NULL;
    s_rx = NULL;
    s_audio = NULL;
    return ESP_ERR_NO_MEM;
}
