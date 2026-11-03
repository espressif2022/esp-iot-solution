/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_config.h"
esp_err_t present_config_resolve_legacy(const esp_display_present_engine_config_t *input, esp_display_present_engine_config_t *out)
{
    if (!input || input->backend_kind < ESP_DISPLAY_PRESENT_ENGINE_GRAM || input->backend_kind > ESP_DISPLAY_PRESENT_ENGINE_FB ||
            input->mode < ESP_DISPLAY_PRESENT_MODE_DEFAULT || input->mode > ESP_DISPLAY_PRESENT_ENGINE_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = *input;
    esp_display_present_present_mode_t mode = input->mode;
    if (mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_AUTO) {
        if (input->backend_kind == ESP_DISPLAY_PRESENT_ENGINE_FB) {
            if (input->backend.buffer_count == 1) {
                mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE;
            } else if (input->backend.buffer_count == 2) {
                mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL;
            } else if (input->backend.buffer_count == 3) {
                mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_PARTIAL;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            mode = input->backend.te_window_us && input->backend.te_transfer_budget_us ? ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC : ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE;
        }
    }
    if (mode == ESP_DISPLAY_PRESENT_MODE_DEFAULT) {
        return ESP_OK;
    }
    if (mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE) {
        out->backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION;
        if (input->backend_kind == ESP_DISPLAY_PRESENT_ENGINE_FB) {
            if (input->backend.external_buffers && input->backend.buffer_count != 1) {
                return ESP_ERR_INVALID_ARG;
            }
            out->backend.buffer_count = 1;
        } else {
            out->backend_kind = ESP_DISPLAY_PRESENT_ENGINE_GRAM;
        }
        return ESP_OK;
    }
    if (mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC) {
        if (input->backend_kind == ESP_DISPLAY_PRESENT_ENGINE_FB) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        out->backend_kind = ESP_DISPLAY_PRESENT_ENGINE_TE;
        out->backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION;
        return ESP_OK;
    }
    if (input->backend_kind != ESP_DISPLAY_PRESENT_ENGINE_FB) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    out->backend.buffer_count = mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_FULL || mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_PARTIAL ? 3 : 2;
    if (input->backend.external_buffers && input->backend.buffer_count != out->backend.buffer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (mode) {
    case ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT:
        out->backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT;
        break;
    case ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL:
    case ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_FULL:
        out->backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL;
        break;
    default:
        out->backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION;
        break;
    }
    return ESP_OK;
}
