#ifndef MIC_H
#define MIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/**
 * @brief Cấu hình phần cứng Audio cho bo mạch LCDWIKI ES3C28P (ESP32-S3 N16R8)
 * Codec: ES8311
 * Microphone: Analog MEMS LMA2718B (nối vào ES8311 ADC)
 * Speaker Amplifier: FM8002E (nối vào ES8311 DAC)
 */

// Chân giao tiếp I2C điều khiển ES8311 (Dùng chung bus I2C với cảm ứng FT6336G & cảm biến APDS-9930)
#define ES8311_I2C_SDA_PIN      GPIO_NUM_16
#define ES8311_I2C_SCL_PIN      GPIO_NUM_15
#define ES8311_I2C_ADDR         0x18         // Địa chỉ 7-bit chuẩn của ES8311

// Chân giao tiếp âm thanh I2S
#define ES8311_I2S_MCLK_PIN     GPIO_NUM_4   // Master Clock (Bắt buộc cho ES8311)
#define ES8311_I2S_BCLK_PIN     GPIO_NUM_5   // Bit Clock (SCLK)
#define ES8311_I2S_WS_PIN       GPIO_NUM_7   // Word Select (LRCK)
#define ES8311_I2S_DIN_PIN      GPIO_NUM_6   // Audio In từ Mic (ASDOUT -> ESP32)
#define ES8311_I2S_DOUT_PIN     GPIO_NUM_8   // Audio Out ra Loa (ESP32 -> DSDIN)
#define ES8311_PA_EN_PIN        GPIO_NUM_1   // Bật/tắt mạch công suất FM8002E (Active Low)
#define SPEAKER_DAC_VOLUME      0xBF         // ES8311 DAC: 0 dB

// Định dạng âm thanh chuẩn cho AI Voice / Speech-to-Text
#define MIC_SAMPLE_RATE         16000        // Tần số lấy mẫu: 16 kHz
#define MIC_DMA_BUFFER_SIZE     1024         // Kích thước buffer DMA

/**
 * @brief Khởi tạo codec ES8311 qua I2C và bus I2S trên bo mạch ES3C28P
 * 
 * @return esp_err_t ESP_OK nếu khởi tạo thành công cả I2C codec và I2S
 */
esp_err_t mic_init(void);

/**
 * @brief Giải phóng tài nguyên I2S và tắt codec
 */
esp_err_t mic_deinit(void);

/**
 * @brief Đọc mẫu âm thanh 16-bit PCM từ microphone LMA2718B
 * 
 * @param dest_pcm16 Con trỏ mảng int16_t lưu mẫu âm thanh
 * @param max_samples Số lượng mẫu tối đa cần đọc
 * @param samples_read Số lượng mẫu thực tế đọc được
 * @param timeout_ms Thời gian chờ tối đa (ms)
 * @return esp_err_t ESP_OK nếu đọc thành công
 */
esp_err_t mic_read_pcm16(int16_t *dest_pcm16, size_t max_samples, size_t *samples_read, uint32_t timeout_ms);

/**
 * @brief Tính năng lượng âm thanh trung bình (biên độ / volume)
 * Dùng cho Voice Activity Detection (VAD) để phát hiện người dùng đang nói
 * 
 * @param samples Mảng mẫu âm thanh 16-bit PCM
 * @param num_samples Số lượng mẫu
 * @return int Giá trị năng lượng âm thanh
 */
int mic_calculate_energy(const int16_t *samples, size_t num_samples);

/**
 * @brief Bật/tắt mạch khuếch đại loa FM8002E (dành cho tính năng phát âm thanh trả lời)
 * 
 * @param enable true để bật loa, false để tắt loa
 */
void speaker_enable(bool enable);

// PCM at the shared I2S rate (16kHz), mono 16-bit. Playback task only.
esp_err_t speaker_write_pcm16(const int16_t *pcm, size_t samples, uint32_t timeout_ms);

#endif // MIC_H
