/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_presenter.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pv2_lvgl pv2_lvgl_t;

/**
 * @brief LVGL 9.5+ PARTIAL/FULL/DIRECT binding, selected from backend caps.
 * draw_rows is used only by PARTIAL; FULL/DIRECT allocate an entire LVGL frame. Caller initializes LVGL, provides tick and pumps
 * LVGL on the presenter's owner task. Caller serializes LVGL access. Exclusive
 * presenter use until binding is destroyed. No global singleton or tick changes.
 * Uses a separately allocated LVGL buffer; DMA never reads this buffer.
 * Binding reserves display user_data, flush callback and draw buffers: callers
 * may create UI objects but must not replace those fields or delete the display
 * directly. RGB888 is normalized from LVGL BGR storage into PV2 R,G,B bytes.
 *
 * @param presenter Presenter owned by the calling task.
 * @param draw_rows LVGL source buffer height for PARTIAL mode.
 * @param out out.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lvgl_create(pv2_presenter_t *presenter, uint32_t draw_rows, pv2_lvgl_t **out);

/**
 * @brief Opt-in zero-copy: FULL/RGB565/native with LVGL-compatible stride only.
 * Renderer leases a backend framebuffer before drawing, and detaches it before
 * flush_ready. Keeps a private fallback buffer for safe failed admission.
 * Other contracts/formats must use the copy binding above.
 *
 * @param presenter Presenter owned by the calling task.
 * @param out out.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lvgl_create_zero_copy(pv2_presenter_t *presenter, pv2_lvgl_t **out);
lv_display_t *pv2_lvgl_get_display(pv2_lvgl_t *binding);

/**
 * @brief Sticky first binding/presenter failure. A failure pauses the refresh timer;
 * no automatic retry of partly transmitted frames. Recreate after recovery.
 *
 * @param binding LVGL binding owned by the calling task.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lvgl_get_error(pv2_lvgl_t *binding);

/**
 * @brief Pause refresh and cancel any open frame; timeout retains the binding for
 * retry. Does not stop/destroy presenter or drain committed frames (they have
 * backend-owned pixel storage). Call on owner task, outside any LVGL callback.
 *
 * @param binding LVGL binding owned by the calling task.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lvgl_destroy(pv2_lvgl_t *binding);

#ifdef __cplusplus
}
#endif
