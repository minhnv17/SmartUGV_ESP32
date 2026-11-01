#include "esp_heap_caps.h"
#include "esp_log.h"
#include "mic.h"
#include "gemini.h"
#include "wifi.h"
#include "speaker.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Khoi dong SmartUGV Gemini Live WebSocket");
    ESP_LOGI(TAG, "PSRAM free: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    esp_err_t ret = mic_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao mic that bai: %s", esp_err_to_name(ret));
        return;
    }
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi chua san sang: %s", esp_err_to_name(ret));
        mic_deinit();
        return;
    }
    ret = speaker_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao loa that bai: %s", esp_err_to_name(ret));
        mic_deinit();
        return;
    }
    ret = gemini_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi dong hoi thoai that bai: %s", esp_err_to_name(ret));
        mic_deinit();
    }
}
