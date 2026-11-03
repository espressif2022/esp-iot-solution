/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_presenter.h"
/* Shared policy, no allocation or hardware access. Legacy V2 defaults unchanged. */
esp_err_t present_config_resolve_legacy(const esp_display_present_engine_config_t *input, esp_display_present_engine_config_t *out);
