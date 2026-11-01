#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mic.h"
#include "speaker.h"

#define FRAME_SAMPLES 480
#define QUEUE_FRAMES 512
#define ECHO_TAIL_MS 600

typedef struct {
    uint32_t epoch;
    size_t count;
    int16_t pcm[FRAME_SAMPLES];
} audio_frame_t;

static const char *TAG = "SPEAKER";
static StaticQueue_t s_queue_state;
static QueueHandle_t s_queue;
static uint8_t *s_queue_memory;
static atomic_uint s_epoch;
static atomic_uint s_last_activity;
static atomic_bool s_active;
static atomic_bool s_turn_finished;
static atomic_uint s_queued_samples;

void speaker_flush(void)
{
    atomic_fetch_add(&s_epoch, 1);
    atomic_store(&s_turn_finished, true);
    speaker_enable(false);
    // The consumer skips old epochs. Do not reset a queue under a producer.
}

void speaker_end_turn(void)
{
    atomic_store(&s_turn_finished, true);
}

bool speaker_is_busy(void)
{
    return s_queue && (uxQueueMessagesWaiting(s_queue) > 0 ||
        (atomic_load(&s_active) && (TickType_t)(xTaskGetTickCount() -
            atomic_load(&s_last_activity)) < pdMS_TO_TICKS(ECHO_TAIL_MS)));
}

void speaker_queue_pcm24(const int16_t *pcm, size_t samples)
{
    if (!s_queue || !pcm) return;
    audio_frame_t frame = {.epoch = atomic_load(&s_epoch)};
    atomic_store(&s_turn_finished, false);
    atomic_store(&s_last_activity, xTaskGetTickCount());
    atomic_store(&s_active, true);
    while (samples) {
        frame.count = samples > FRAME_SAMPLES ? FRAME_SAMPLES : samples;
        memcpy(frame.pcm, pcm, frame.count * sizeof(int16_t));
        atomic_fetch_add(&s_queued_samples, frame.count);
        if (xQueueSend(s_queue, &frame, 0) != pdTRUE) {
            atomic_fetch_sub(&s_queued_samples, frame.count);
            speaker_flush();
            ESP_LOGW(TAG, "Playback queue full; discard pending audio");
            return;
        }
        pcm += frame.count;
        samples -= frame.count;
    }
}

static void playback_task(void *argument)
{
    // 15-tap Hamming-windowed low-pass at 6.96kHz, Q15, unity DC gain.
    // Filter at 24kHz before converting 3 input samples to 2 output samples.
    static const int16_t taps[15] = {
        22, -218, 163, 964, -1629, -2079, 9650, 19022,
        9650, -2079, -1629, 964, 163, -218, 22
    };
    audio_frame_t frame;
    int16_t history[15] = {0};
    int16_t output[FRAME_SAMPLES];
    unsigned position = 0, phase = 0;
    int16_t midpoint = 0;
    uint32_t epoch = atomic_load(&s_epoch);
    bool playing = false;
    size_t played = 0;
    unsigned prefill_ms = 800;
    TickType_t last_write = 0;
    unsigned largest_gap_ms = 0;
    bool starved_turn = false;
    for (;;) {
        if (xQueueReceive(s_queue, &frame, pdMS_TO_TICKS(500)) != pdTRUE) {
            // TX auto-clears to silence. Wait longer than the 90ms DMA depth
            // before disabling the amplifier; suppress mic for another tail.
            if (playing) {
                if (!atomic_load(&s_turn_finished)) {
                    prefill_ms = prefill_ms < 1400 ? prefill_ms + 200 : 1600;
                    ESP_LOGW(TAG, "Thieu audio mang; tang dem dot sau len %u ms", prefill_ms);
                } else if (!starved_turn && prefill_ms > 400) {
                    prefill_ms -= 50;
                }
                speaker_enable(false);
                ESP_LOGI(TAG, "Phat xong %u mau PCM 16kHz", (unsigned)played);
                ESP_LOGI(TAG, "Khoang cho du lieu lon nhat=%u ms", largest_gap_ms);
                largest_gap_ms = 0;
                starved_turn = false;
                playing = false;
                played = 0;
            }
            memset(history, 0, sizeof(history));
            phase = position = 0;
            continue;
        }
        atomic_fetch_sub(&s_queued_samples, frame.count);
        uint32_t current = atomic_load(&s_epoch);
        if (frame.epoch != current) continue;
        if (playing) {
            unsigned gap = (xTaskGetTickCount() - last_write) * portTICK_PERIOD_MS;
            if (gap > largest_gap_ms) largest_gap_ms = gap;
            if (gap > 90 && !atomic_load(&s_turn_finished)) {
                starved_turn = true;
                unsigned extra = gap - 90 + 50;
                prefill_ms = prefill_ms + extra > 1600 ? 1600 : prefill_ms + extra;
                ESP_LOGW(TAG, "Queue starvation %u ms; target=%u ms of PCM", gap, prefill_ms);
            }
        }
        if (!playing) {
            // Power the amplifier while buffering, rather than at the first
            // voice sample. TX is already outputting zeros via auto-clear.
            speaker_enable(true);
            // Wait for actual PCM duration, not just elapsed wall time.
            // A short, complete response can start before reaching the target.
            TickType_t began = xTaskGetTickCount();
            while ((atomic_load(&s_queued_samples) + frame.count) < prefill_ms * 24 &&
                   !atomic_load(&s_turn_finished) && current == atomic_load(&s_epoch) &&
                   xTaskGetTickCount() - began < pdMS_TO_TICKS(2500)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            TickType_t warmup = pdMS_TO_TICKS(200);
            TickType_t elapsed = xTaskGetTickCount() - began;
            if (elapsed < warmup) vTaskDelay(warmup - elapsed);
            ESP_LOGI(TAG, "Buffered PCM=%u ms, target=%u ms",
                (unsigned)((atomic_load(&s_queued_samples) + frame.count) / 24), prefill_ms);
            if (current != atomic_load(&s_epoch)) continue;
        }
        if (epoch != current) {
            memset(history, 0, sizeof(history));
            phase = position = 0;
            epoch = current;
        }
        size_t count = 0;
        for (size_t i = 0; i < frame.count; ++i) {
            history[position] = frame.pcm[i];
            int64_t sum = 0;
            for (unsigned tap = 0; tap < 15; ++tap) {
                sum += (int32_t)taps[tap] * history[(position + 15 - tap) % 15];
            }
            position = (position + 1) % 15;
            int value = (int)(sum / 32768);
            if (value > INT16_MAX) value = INT16_MAX;
            if (value < INT16_MIN) value = INT16_MIN;
            if (phase == 0) output[count++] = value;
            else if (phase == 1) midpoint = value;
            else output[count++] = ((int)midpoint + value) / 2;
            phase = (phase + 1) % 3;
        }
        if (epoch != atomic_load(&s_epoch) || count == 0) continue;
        atomic_store(&s_last_activity, xTaskGetTickCount());
        if (!playing) {
            ESP_LOGI(TAG, "Dang phat Gemini qua loa");
            playing = true;
        }
        esp_err_t ret = speaker_write_pcm16(output, count, 100);
        last_write = xTaskGetTickCount();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S TX: %s", esp_err_to_name(ret));
            speaker_flush();
            playing = false;
        } else played += count;
    }
}

esp_err_t speaker_start(void)
{
    if (s_queue) return ESP_ERR_INVALID_STATE;
    s_queue_memory = heap_caps_malloc(QUEUE_FRAMES * sizeof(audio_frame_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_queue_memory) return ESP_ERR_NO_MEM;
    s_queue = xQueueCreateStatic(QUEUE_FRAMES, sizeof(audio_frame_t), s_queue_memory, &s_queue_state);
    if (s_queue && xTaskCreate(playback_task, "speaker", 6144, NULL, 5, NULL) == pdPASS) return ESP_OK;
    if (s_queue) vQueueDelete(s_queue);
    s_queue = NULL;
    free(s_queue_memory);
    s_queue_memory = NULL;
    return ESP_ERR_NO_MEM;
}
