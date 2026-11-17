/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_renderer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optional bounded completion observer token (32 per presenter, independent of buffer ownership).
 */
typedef struct {
    uint64_t value; /**< Instance-scoped receipt token (0 is invalid). */
} esp_display_present_engine_receipt_t;

#ifdef __cplusplus
}
#endif
