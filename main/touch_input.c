#include <stdatomic.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "touch_input.h"
#include "touch_ui.h"
#include "robot_face.h"
#include "music.h"

static const char *TAG = "TOUCH_FT6336";
static i2c_master_dev_handle_t s_device;
static atomic_bool s_available;

bool touch_input_available(void) { return atomic_load(&s_available); }

static void touch_task(void *arg)
{
    touch_gesture_t gesture = {0};
    unsigned failures = 0;
    for (;;) {
        uint8_t reg = 0x02, data[5];
        esp_err_t ret = i2c_master_transmit_receive(s_device, &reg, 1, data, sizeof(data), 20);
        unsigned contacts = 0;
        int x = 0, y = 0;
#ifdef CONFIG_ROBOT_FACE_FLIP_LANDSCAPE
        const bool flip = true;
#else
        const bool flip = false;
#endif
        bool valid = ret == ESP_OK && touch_decode(data, flip, &contacts, &x, &y);
        if (!valid) {
            if (failures < 3) ++failures;
            if (failures == 3 && atomic_exchange(&s_available, false))
                ESP_LOGW(TAG, "Touch reads failed; controls disabled until sensor recovers");
        } else {
            failures = 0;
            atomic_store(&s_available, true);
        }
        touch_action_t action = touch_gesture_step(&gesture, robot_face_touch_view(), valid, contacts, x, y);
        const char *command = action == TOUCH_ACTION_PAUSE ? "pause" :
                              action == TOUCH_ACTION_RESUME ? "resume" :
                              action == TOUCH_ACTION_STOP ? "stop" : NULL;
        if (command) {
            ret = music_control(command);
            ESP_LOGI(TAG, "Touch %s: %s", command, esp_err_to_name(ret));
        }
        if (action == TOUCH_ACTION_VOLUME_DOWN || action == TOUCH_ACTION_VOLUME_UP || action == TOUCH_ACTION_VOLUME_SET) {
            int volume = music_get_volume();
            volume = action == TOUCH_ACTION_VOLUME_DOWN ? volume - 5 :
                     action == TOUCH_ACTION_VOLUME_UP ? volume + 5 : (int)gesture.volume_percent;
            unsigned percent = volume < 0 ? 0 : volume > 100 ? 100 : (unsigned)volume;
            music_set_volume(percent);
            ESP_LOGI(TAG, "Touch volume: %u%%", percent);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t touch_input_start(void)
{
    if (s_device) return ESP_ERR_INVALID_STATE;
    i2c_master_bus_handle_t bus;
    esp_err_t ret = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
    if (ret != ESP_OK) return ret;
    gpio_config_t reset = {.pin_bit_mask = 1ULL << GPIO_NUM_18, .mode = GPIO_MODE_OUTPUT};
    if ((ret = gpio_config(&reset)) != ESP_OK) return ret;
    gpio_set_level(GPIO_NUM_18, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_NUM_18, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    if ((ret = i2c_master_probe(bus, 0x38, 50)) != ESP_OK) return ret;
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x38, .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(bus, &config, &s_device);
    if (ret != ESP_OK) return ret;
    const uint8_t normal_mode[] = {0x00, 0x00};
    ret = i2c_master_transmit(s_device, normal_mode, sizeof(normal_mode), 50);
    if (ret != ESP_OK) goto fail;
    const uint8_t polling_mode[] = {0xa4, 0x00};
    ret = i2c_master_transmit(s_device, polling_mode, sizeof(polling_mode), 50);
    if (ret != ESP_OK) goto fail;
    if (xTaskCreate(touch_task, "touch_input", 3072, NULL, 2, NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    // The first valid sensor sample enables hit testing.
    ESP_LOGI(TAG, "FT6336 on shared I2C0 ready; touch controls enabled");
    return ESP_OK;
fail:
    i2c_master_bus_rm_device(s_device);
    s_device = NULL;
    return ret;
}
