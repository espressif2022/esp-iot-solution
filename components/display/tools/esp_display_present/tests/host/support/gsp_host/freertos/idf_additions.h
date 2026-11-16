/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "freertos/task.h"

#define xTaskCreateWithCaps(entry, name, stack, arg, priority, task, caps) \
    xTaskCreate((entry), (name), (stack), (arg), (priority), (task))
#define vTaskDeleteWithCaps(task) vTaskDelete(task)
