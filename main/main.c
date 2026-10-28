#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "wifi.h"
#include "gemini.h"

static const char* TAG = "APP_MAIN";

static void gemini_task(void* pvParameters) {
    ESP_LOGI(TAG, "Khoi dong Gemini Task...");

    // Doi 1 giay de ket noi mang hoan toan on dinh
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Buffer chua phan hoi tu Gemini (2KB la du cho cac cau tra loi van ban ngan)
    char response[2048] = { 0 };
    const char* prompt = "Xin chao Gemini! Ban hay tu gioi thieu ngan gon ve minh trong 1 cau tieng Viet.";

    ESP_LOGI(TAG, "--- GUI CAU HOI TOI GEMINI ---");
    ESP_LOGI(TAG, "Prompt: %s", prompt);

    esp_err_t ret = gemini_generate_content(prompt, response, sizeof(response));

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "--- KET QUA PHAN HOI TU GEMINI AI ---");
        printf("\n>>> Gemini: %s\n\n", response);
        ESP_LOGI(TAG, "-------------------------------------");
    }
    else {
        ESP_LOGE(TAG, "Goi Gemini API that bai! Chi tiet: %s", response[0] != '\0' ? response : esp_err_to_name(ret));
    }

    // Xoa task sau khi thuc hien xong
    vTaskDelete(NULL);
}

void obtain_time(void)
{
    ESP_LOGI("SNTP", "Đang đồng bộ thời gian từ NTP server...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Chờ cho đến khi thời gian được đồng bộ
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
    {
        ESP_LOGI("SNTP", "Đang chờ đồng bộ thời gian... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI("SNTP", "Thời gian hiện tại: %s", asctime(&timeinfo));
}

void app_main(void) {
    ESP_LOGI(TAG, "Khoi dong SmartUGV ESP32...");

    if (wifi_init_sta() == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi da san sang su dung!");
        // obtain_time();
        // Tao task rieng cho Gemini voi stack 10KB (danh cho mbedTLS handshake an toan)
        xTaskCreate(gemini_task, "gemini_task", 10240, NULL, 5, NULL);
    }
    else {
        ESP_LOGE(TAG, "Loi ket noi Wi-Fi! Vui long kiem tra lai SSID va Password.");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "SmartUGV heartbeat...");
    }
}
