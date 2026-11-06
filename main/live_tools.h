#pragma once
#include <stdbool.h>
#include "cJSON.h"
#include "esp_err.h"

esp_err_t live_tools_start(void);
void live_tools_reset(void);
bool live_tools_submit(cJSON *tool_call);
void live_tools_cancel(cJSON *cancellation);
// Caller owns a returned JSON string and releases it with cJSON_free().
bool live_tools_take_reply(char **json);
bool live_tools_failed(void);
// Attach available function declarations to the setup object.
bool live_tools_add_declarations(cJSON *setup);
