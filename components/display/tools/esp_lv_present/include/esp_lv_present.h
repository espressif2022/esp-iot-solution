/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file esp_lv_present.h
 * @brief Bind an LVGL 9 display to an existing esp_display_present presenter.
 *
 * With LVGL 9.5 or later, compatible PARTITION layouts can render into leased
 * tiles, and compatible DIRECT and FULL layouts can use framebuffer leases.
 * Earlier LVGL 9 releases and incompatible layouts use a separate LVGL draw
 * buffer and copy on flush. Pump LVGL on the task that called esp_lv_present_start().
 */

#include <stdint.h>

#include "esp_err.h"
#include "esp_display_present.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an LVGL display on @p presenter and install the flush path.
 *
 * Rebinds the presenter producer to the calling task. Initializes LVGL and an
 * esp_timer tick on the first successful call. Supports PARTITION, DIRECT,
 * and FULL contracts.
 *
 * @param[in]  presenter Presenter already created by the application.
 * @param[out] ret_disp  Created LVGL display handle.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: @p presenter or @p ret_disp is NULL
 *      - ESP_ERR_INVALID_STATE: Binding already started
 *      - ESP_ERR_NOT_SUPPORTED: Unsupported contract, pixel format, or buffer layout
 *      - ESP_ERR_NO_MEM: Failed to create the LVGL display
 */
esp_err_t esp_lv_present_start(esp_display_presenter_t *presenter,
                               lv_display_t **ret_disp);

/**
 * @brief Tear down the LVGL display bound by esp_lv_present_start().
 *
 * Cancels any in-flight presenter frame first. The presenter itself is not
 * deleted; borrowed draw buffers remain owned by the presenter.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Binding is not started
 */
esp_err_t esp_lv_present_stop(void);

/**
 * @brief Rotate the active display without recreating its LVGL objects.
 *
 * Call on the task that started the binding, serialized with LVGL, outside
 * LVGL callbacks. Drains pending output and updates logical resolution,
 * Presenter point mapping and generation; forces a full redraw. Does not
 * change panel MADCTL or reset application input devices. Reset/wait-release
 * touch input after this call before accepting new coordinates.
 *
 * Allocation failure or a drain timeout before rotation preserves the old
 * orientation. A failure after the engine changes orientation pauses rendering;
 * Presenter queries report the actual orientation. Stop/recreate the binding
 * before resuming in that case; do not overwrite LVGL draw buffers manually.
 *
 * @param[in]  rotation   Target logical-to-physical rotation angle.
 * @param[in]  timeout_ms Maximum time in milliseconds to drain pending output before rotating.
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG for invalid angles, ESP_ERR_INVALID_STATE
 * for no binding/wrong task/refresh in progress, or allocation/backend error.
 */
esp_err_t esp_lv_present_set_rotation(esp_display_present_rotation_t rotation, uint32_t timeout_ms);

/**
 * @brief Return frames committed to the presenter since the last start.
 *
 * @return Committed frame count, or 0 when the binding is not started.
 */
uint32_t esp_lv_present_get_frame_count(void);

#ifdef __cplusplus
}
#endif
