#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "mic.h"

static const char *TAG = "AUDIO_ES8311";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_es8311_dev = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_tx_enabled = false;
static bool s_rx_enabled = false;
static bool s_initialized = false;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
    if (!s_es8311_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_transmit(s_es8311_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 reg 0x%02x <- 0x%02x: %s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t es8311_read_reg(uint8_t reg, uint8_t *val) {
    if (!s_es8311_dev || !val) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_es8311_dev, &reg, 1, val, 1, 100);
}

/**
 * @brief Cấu hình chip Codec ES8311 cho microphone analog LMA2718B ở tần số 16kHz
 */
static esp_err_t es8311_codec_init(void) {
    ESP_LOGI(TAG, "Dang cau hinh codec ES8311 qua I2C...");

    // ES8311 bias/power sequence follows Espressif's ES8311 driver:
    // https://github.com/espressif/esp-adf/blob/release/v2.x/components/esp_codec_dev/device/es8311/es8311.c
    // Fixed configuration: external MCLK = 256 * 16 kHz, slave, Philips 16-bit.
    esp_err_t ret = es8311_write_reg(0x00, 0x1F);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));

    static const uint8_t setup[][2] = {
        {0x44, 0x08}, // ADC in both slots; improve I2C noise immunity
        {0x01, 0x30}, // Enable external clocks before releasing reset
        {0x02, 0x00}, // MCLK pre-divider = 1, multiplier = 1
        {0x03, 0x10}, // ADC oversampling
        {0x04, 0x20}, // DAC oversampling
        {0x05, 0x00}, // ADC/DAC clock dividers = 1
        {0x06, 0x03}, // BCLK divider (codec is slave)
        {0x07, 0x00}, // LRCK divider high bits
        {0x08, 0xFF}, // LRCK divider low bits: 256 * Fs
        {0x0B, 0x00}, // Power-up timing
        {0x0C, 0x00},
        {0x10, 0x1F}, // Analog bias/reference settings required at startup
        {0x11, 0x7F}, // Analog reference setting
        {0x00, 0x80}, // Release reset, CSM on, codec is I2S slave
        {0x01, 0x3F}, // Enable analog and digital clocks
        {0x09, 0x0C}, // DAC input unmuted, Philips 16-bit
        {0x0A, 0x0C}, // ADC output unmuted, Philips 16-bit
        {0x13, 0x10},
        {0x1B, 0x0A}, // ADC high-pass filter
        {0x1C, 0x6A},
        {0x17, 0xBF}, // ADC digital volume = 0 dB (0xC8 is +4.5 dB)
        {0x0E, 0x02}, // Enable analog PGA and ADC modulator
        {0x12, 0x00}, // Enable DAC for speaker playback
        {0x32, SPEAKER_DAC_VOLUME}, // DAC volume 0 dB
        {0x37, 0x08}, // Bypass DAC EQ
        {0x14, 0x1A}, // Analog differential mic input, PGA +30 dB
        {0x16, 0x24}, // Standard-clock ADC sync, +24 dB digital scale
        {0x0D, 0x01}, // Enable analog bias and start VMID charging
        {0x15, 0x40}, // ADC volume ramp
        {0x45, 0x00},
    };
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        ret = es8311_write_reg(setup[i][0], setup[i][1]);
        if (ret != ESP_OK) return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read back critical settings instead of reporting success after writes alone.
    static const uint8_t verify[][2] = {
        {0x00, 0x80}, {0x01, 0x3F}, {0x0A, 0x0C}, {0x10, 0x1F},
        {0x11, 0x7F}, {0x14, 0x1A}, {0x16, 0x24}, {0x17, 0xBF}, {0x44, 0x08},
        {0x09, 0x0C}, {0x12, 0x00}, {0x32, SPEAKER_DAC_VOLUME},
    };
    for (size_t i = 0; i < sizeof(verify) / sizeof(verify[0]); ++i) {
        uint8_t value = 0;
        ret = es8311_read_reg(verify[i][0], &value);
        if (ret != ESP_OK) return ret;
        ESP_LOGI(TAG, "ES8311 reg 0x%02x = 0x%02x", verify[i][0], value);
        if (value != verify[i][1]) {
            ESP_LOGE(TAG, "Readback mismatch: expected 0x%02x", verify[i][1]);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI(TAG, "Khoi tao Codec ES8311 thanh cong!");
    return ESP_OK;
}

void speaker_enable(bool enable) {
    // FM8002E amplifier tren bo mach ES3C28P duoc dieu khien qua GPIO 1 (Active Low)
    gpio_set_level(ES8311_PA_EN_PIN, enable ? 0 : 1);
}

esp_err_t mic_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Audio subsystem da duoc khoi tao truoc do");
        return ESP_OK;
    }

    // 1. Cấu hình chân PA_EN cho loa (tắt loa trước khi khởi động để tránh tiếng bụp)
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << ES8311_PA_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&pa_cfg);
    if (ret != ESP_OK) return ret;
    speaker_enable(false);

    // 2. Khởi tạo bus I2C Master (dùng chung cho ES8311, cảm ứng FT6336G và cảm biến APDS-9930)
    i2c_master_bus_config_t i2c_mst_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = ES8311_I2C_SDA_PIN,
        .scl_io_num = ES8311_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_mst_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao I2C master bus that bai: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = i2c_master_probe(s_i2c_bus, ES8311_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khong tim thay ES8311 tai 0x18 (SDA=16, SCL=15): %s", esp_err_to_name(ret));
        goto fail;
    }

    // Đăng ký thiết bị ES8311 trên bus I2C
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_es8311_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Them ES8311 vao I2C bus that bai: %s", esp_err_to_name(ret));
        goto fail;
    }

    // 3. Khởi tạo Channel I2S Master RX trên ESP32-S3
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.auto_clear = true;
    chan_cfg.dma_frame_num = 240;

    ret = i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tao I2S channel that bai: %s", esp_err_to_name(ret));
        goto fail;
    }

    // 4. Cấu hình I2S Standard Philips Slot với MCLK xuất ra GPIO 4
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = ES8311_I2S_MCLK_PIN, // GPIO 4: MCLK cấp cho ES8311
            .bclk = ES8311_I2S_BCLK_PIN, // GPIO 5: BCLK
            .ws   = ES8311_I2S_WS_PIN,   // GPIO 7: LRCK
            .dout = ES8311_I2S_DOUT_PIN,
            .din  = ES8311_I2S_DIN_PIN,  // GPIO 6: Audio Data Input tu ES8311 ASDOUT
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao I2S std mode that bai: %s", esp_err_to_name(ret));
        goto fail;
    }

    // Full duplex: both directions share MCLK/BCLK/LRCK at 16 kHz.
    ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (ret != ESP_OK) goto fail;
    ret = i2s_channel_enable(s_tx_handle);
    if (ret != ESP_OK) goto fail;
    s_tx_enabled = true;

    // 5. Kích hoạt channel I2S trước để có MCLK ổn định cho ES8311
    ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable I2S channel that bai: %s", esp_err_to_name(ret));
        goto fail;
    }

    s_rx_enabled = true;
    vTaskDelay(pdMS_TO_TICKS(10));

    // 6. Gửi cấu hình khởi tạo các thanh ghi codec ES8311 qua I2C
    ret = es8311_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cau hinh ES8311 that bai!");
        goto fail;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "I2S RX: Fs=16000, MCLK=4096000, BCLK=512000, left slot, 16-bit");
    ESP_LOGI(TAG, "Microphone LMA2718B (qua ES8311) da san sang hoat dong!");
    return ESP_OK;

fail:
    mic_deinit();
    return ret;
}

esp_err_t mic_deinit(void) {
    s_initialized = false;
    speaker_enable(false);
    if (s_tx_handle) {
        if (s_tx_enabled) i2s_channel_disable(s_tx_handle);
        s_tx_enabled = false;
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
    }
    if (s_rx_handle) {
        if (s_rx_enabled) i2s_channel_disable(s_rx_handle);
        s_rx_enabled = false;
        i2s_del_channel(s_rx_handle);
        s_rx_handle = NULL;
    }
    if (s_es8311_dev) {
        // Power down ES8311
        es8311_write_reg(0x00, 0x1F);
        i2c_master_bus_rm_device(s_es8311_dev);
        s_es8311_dev = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    return ESP_OK;
}

esp_err_t mic_read_pcm16(int16_t *dest_pcm16, size_t max_samples, size_t *samples_read, uint32_t timeout_ms) {
    if (samples_read) *samples_read = 0;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!dest_pcm16 || max_samples == 0 || max_samples > SIZE_MAX / sizeof(int16_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_rx_handle, dest_pcm16, max_samples * sizeof(int16_t), &bytes_read, timeout_ms);
    if (samples_read) {
        *samples_read = bytes_read / sizeof(int16_t);
    }
    return ret;
}

int mic_calculate_energy(const int16_t *samples, size_t num_samples) {
    if (!samples || num_samples == 0) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < num_samples; i++) {
        sum += abs((int32_t)samples[i]);
    }
    return (int)(sum / (int64_t)num_samples);
}

esp_err_t speaker_write_pcm16(const int16_t *pcm, size_t samples, uint32_t timeout_ms)
{
    if (!s_initialized || !s_tx_enabled) return ESP_ERR_INVALID_STATE;
    if (!pcm || !samples || samples > SIZE_MAX / sizeof(int16_t)) return ESP_ERR_INVALID_ARG;
    size_t written = 0;
    esp_err_t ret = i2s_channel_write(s_tx_handle, pcm, samples * sizeof(int16_t), &written, timeout_ms);
    if (ret == ESP_OK && written != samples * sizeof(int16_t)) return ESP_ERR_INVALID_SIZE;
    return ret;
}
