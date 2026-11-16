/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "freertos/queue.h"

typedef QueueHandle_t SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
#define xSemaphoreGive(sem) xQueueSend((sem), NULL, 0)
#define xSemaphoreTake(sem, wait) xQueueReceive((sem), NULL, (wait))
#define vSemaphoreDelete(sem) vQueueDelete(sem)
