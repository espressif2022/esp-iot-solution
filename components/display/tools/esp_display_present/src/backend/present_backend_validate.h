/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validate backend configuration constraints shared by plan resolve and backend create.
 *
 * Enforces dimensions, pixel format, byte orders, rotation, render-contract
 * range, and frame-record bounds, all returning ESP_ERR_INVALID_ARG.
 * Layer-specific rules remain with their owners.
 */
esp_err_t present_backend_config_validate_shared(const esp_display_present_backend_config_t *config);

#ifdef __cplusplus
}
#endif
