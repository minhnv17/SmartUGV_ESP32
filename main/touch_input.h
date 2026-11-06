#pragma once
#include <stdbool.h>
#include "esp_err.h"
// Reuse the I2C0 bus initialized by mic_init(); never create another bus.
esp_err_t touch_input_start(void);
bool touch_input_available(void);
