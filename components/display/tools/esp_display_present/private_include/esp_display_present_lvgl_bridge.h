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
 * @brief Private renderer interface shared by esp_display_present and esp_lv_present.
 *
 * All calls are serialized on the bound producer task.
 */
struct esp_display_presenter;

/** @brief Bind a presenter session and transfer engine ownership to the calling producer task. */
esp_err_t present_session_bind(struct esp_display_presenter *presenter, esp_display_present_engine_t **out_engine);
/** @brief Unbind the active renderer session and restore default presenter ownership. */
void present_session_unbind(struct esp_display_presenter *presenter);
/** @brief Return the committed frame counter observed across the session. */
uint64_t present_session_committed(struct esp_display_presenter *presenter);

/** @brief Inspect pool addresses and writable capacity per slot (writes still require a lease). */
esp_err_t esp_display_present_engine_get_draw_buffers(esp_display_present_engine_t *presenter, void **out_buffers,
                                                      size_t capacity, size_t *count, size_t *bytes);
/** @brief Query backend capabilities seen by the bound renderer. */
esp_err_t esp_display_present_engine_get_caps(esp_display_present_engine_t *presenter, esp_display_present_backend_caps_t *caps);

/** @brief Pump pending backend work when needed; definite TE rejection may retry on a later edge. */
esp_err_t esp_display_present_engine_service(esp_display_present_engine_t *presenter);
/** @brief Reap completed receipts and report lifecycle state without granting leases. */
esp_err_t esp_display_present_engine_get_status(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *status);

/** @brief Begin a new frame within the configured wait budget; timeout leaves prior state intact. */
esp_err_t esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *presenter,
                                                      const esp_display_present_frame_request_t *request);
/** @brief Acquire a writable surface lease for the active frame. */
esp_err_t esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *presenter, esp_display_present_lease_t *lease);
/** @brief Resolve how many rows fit for a chosen stride in an unconsumed lease. */
esp_err_t esp_display_present_engine_resolve_rows(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                                  size_t stride_bytes, uint32_t requested_rows, uint32_t *out_rows);
/** @brief Submit the rendered region of an acquired lease. */
esp_err_t esp_display_present_engine_submit_buffer(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                                   const esp_display_present_engine_area_t *area, size_t stride_bytes);
/** @brief Seal the active frame; optional coverage supplements actual write tracking. */
esp_err_t esp_display_present_engine_commit_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage);
/** @brief Cancel the active frame; timeout keeps the frame context for retry. */
esp_err_t esp_display_present_engine_cancel_frame(esp_display_present_engine_t *presenter);

/** @brief Expand logical damage against physical panel region constraints before drawing. */
esp_err_t esp_display_present_engine_resolve_render_area(esp_display_present_engine_t *presenter,
                                                         const esp_display_present_rect_t *area, esp_display_present_rect_t *out);
/** @brief Query the active logical-to-physical engine rotation. */
esp_err_t esp_display_present_engine_get_rotation(esp_display_present_engine_t *presenter, esp_display_present_engine_rotation_t *out);
/**
 * @brief Drain pending frames and reconfigure logical orientation without changing physical panel axes.
 *
 * Detach and recreate any renderer bound to old caps; forces a full redraw.
 */
esp_err_t esp_display_present_engine_set_rotation(esp_display_present_engine_t *presenter,
                                                  esp_display_present_engine_rotation_t rotation, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
