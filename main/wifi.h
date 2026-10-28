#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

// Thay đổi SSID và Password của Wi-Fi nhà bạn tại đây
#define EXAMPLE_ESP_WIFI_SSID "TP-Link_A5E0"
#define EXAMPLE_ESP_WIFI_PASS "49342543"

/**
 * @brief Khởi tạo Wi-Fi ở chế độ Station (STA)
 */
esp_err_t wifi_init_sta(void);

#endif // WIFI_H