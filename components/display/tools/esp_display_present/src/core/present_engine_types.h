/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_renderer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional, bounded observer records (32 per presenter), independent of backend
 * buffer ownership. Tokens are instance-scoped; callers explicitly release a
 * terminal observer or cancel tracking. Untracked commit remains unchanged. */
typedef struct {
    uint64_t value;
} esp_display_present_engine_receipt_t;

#ifdef __cplusplus
}
#endif
