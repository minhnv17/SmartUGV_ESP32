#pragma once
#include <stddef.h>
#include "FreeRTOS.h"
typedef struct host_queue *QueueHandle_t;
typedef int StaticQueue_t;
QueueHandle_t xQueueCreateStatic(unsigned length, unsigned size, uint8_t *memory, StaticQueue_t *state);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
