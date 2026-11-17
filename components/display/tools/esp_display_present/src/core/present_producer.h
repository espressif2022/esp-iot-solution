/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "core/present_engine_types.h"
#include "backend/present_backend_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Begin a frame on the owner task within an explicit acquisition budget. */
esp_err_t present_presenter_begin_budget(esp_display_present_engine_t *, const esp_display_present_frame_request_t *, uint32_t);
/** @brief Cancel the active pre-commit frame on the owner task within an explicit budget. */
esp_err_t present_presenter_cancel_budget(esp_display_present_engine_t *, uint32_t);

/** @brief Resolve legal rows for @p stride in an unconsumed lease on the owner task. */
esp_err_t present_presenter_resolve_lease_rows(esp_display_present_engine_t *, esp_display_present_lease_id_t,
                                               size_t stride, uint32_t requested, uint32_t *out);
/** @brief Submit rendered buffer contents for an acquired lease on the owner task. */
esp_err_t present_presenter_submit_lease(esp_display_present_engine_t *, esp_display_present_lease_id_t, void *pixels,
                                         const esp_display_present_engine_area_t *, size_t stride);

#ifdef __cplusplus
}
#endif
