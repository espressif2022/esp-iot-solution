/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_config.h"
esp_err_t pv2_config_resolve_legacy(const pv2_presenter_config_t *input, pv2_presenter_config_t *out)
{
    if (!input || input->backend_kind < PV2_PRESENTER_GRAM || input->backend_kind > PV2_PRESENTER_FB ||
            input->mode < PV2_MODE_DEFAULT || input->mode > PV2_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = *input;
    pv2_present_mode_t mode = input->mode;
    if (mode == PV2_MODE_AUTO) {
        if (input->backend_kind == PV2_PRESENTER_FB) {
            if (input->backend.buffer_count == 1) {
                mode = PV2_MODE_NONE;
            } else if (input->backend.buffer_count == 2) {
                mode = PV2_MODE_DOUBLE_PARTIAL;
            } else if (input->backend.buffer_count == 3) {
                mode = PV2_MODE_TRIPLE_PARTIAL;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            mode = input->backend.te_window_us && input->backend.te_transfer_budget_us ? PV2_MODE_TE_SYNC : PV2_MODE_NONE;
        }
    }
    if (mode == PV2_MODE_DEFAULT) {
        return ESP_OK;
    }
    if (mode == PV2_MODE_NONE) {
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        if (input->backend_kind == PV2_PRESENTER_FB) {
            if (input->backend.external_buffers && input->backend.buffer_count != 1) {
                return ESP_ERR_INVALID_ARG;
            }
            out->backend.buffer_count = 1;
        } else {
            out->backend_kind = PV2_PRESENTER_GRAM;
        }
        return ESP_OK;
    }
    if (mode == PV2_MODE_TE_SYNC) {
        if (input->backend_kind == PV2_PRESENTER_FB) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        out->backend_kind = PV2_PRESENTER_TE;
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        return ESP_OK;
    }
    if (input->backend_kind != PV2_PRESENTER_FB) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    out->backend.buffer_count = mode == PV2_MODE_TRIPLE_FULL || mode == PV2_MODE_TRIPLE_PARTIAL ? 3 : 2;
    if (input->backend.external_buffers && input->backend.buffer_count != out->backend.buffer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (mode) {
    case PV2_MODE_DOUBLE_DIRECT:
        out->backend.render_contract = PV2_CONTRACT_DIRECT;
        break;
    case PV2_MODE_DOUBLE_FULL:
    case PV2_MODE_TRIPLE_FULL:
        out->backend.render_contract = PV2_CONTRACT_FULL;
        break;
    default:
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        break;
    }
    return ESP_OK;
}
