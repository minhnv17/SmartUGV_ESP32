#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
BaseType_t xTaskCreate(void (*function)(void *), const char *name, unsigned stack, void *argument, unsigned priority, void *handle);
