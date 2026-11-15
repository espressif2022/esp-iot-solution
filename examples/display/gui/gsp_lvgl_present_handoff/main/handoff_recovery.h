/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

typedef enum {
    HANDOFF_QUIESCE_LVGL,
    HANDOFF_STOP_LVGL,
    HANDOFF_RESUME_GSP,
    HANDOFF_RECOVERED,
} handoff_recovery_stage_t;

typedef struct {
    esp_err_t (*quiesce_lvgl)(void *context);
    esp_err_t (*stop_lvgl)(void *context);
    esp_err_t (*resume_gsp)(void *context);
} handoff_recovery_ops_t;

/* Advance only after the current operation succeeds. The caller retains its
 * context, including the pause token, until recovery completes. */
static inline esp_err_t handoff_recover(handoff_recovery_stage_t *stage,
                                        const handoff_recovery_ops_t *ops,
                                        void *context)
{
    if (*stage == HANDOFF_QUIESCE_LVGL) {
        esp_err_t error = ops->quiesce_lvgl(context);
        if (error != ESP_OK) {
            return error;
        }
        *stage = HANDOFF_STOP_LVGL;
    }
    if (*stage == HANDOFF_STOP_LVGL) {
        esp_err_t error = ops->stop_lvgl(context);
        if (error != ESP_OK) {
            return error;
        }
        *stage = HANDOFF_RESUME_GSP;
    }
    if (*stage == HANDOFF_RESUME_GSP) {
        esp_err_t error = ops->resume_gsp(context);
        if (error != ESP_OK) {
            return error;
        }
        *stage = HANDOFF_RECOVERED;
    }
    return ESP_OK;
}
