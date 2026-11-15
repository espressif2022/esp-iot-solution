/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_renderer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private renderer interface shared by esp_display_present and esp_lv_present.
 * Calls are serialized on the bound producer task. */
struct esp_display_presenter;
/* A successful bind transfers ownership. Failed bind preserves producer
 * identity. Unbind ends the session and leaves the calling task as producer. */
esp_err_t present_session_bind(struct esp_display_presenter *presenter, esp_display_present_engine_t **out_engine);
void present_session_unbind(struct esp_display_presenter *presenter);
uint64_t present_session_committed(struct esp_display_presenter *presenter);

/* Inspect pool addresses and writable capacity per slot. Every write requires
 * acquiring a lease first, including writes by an external renderer. */
esp_err_t esp_display_present_engine_get_draw_buffers(esp_display_present_engine_t *presenter, void **out_buffers,
                                                      size_t capacity, size_t *count, size_t *bytes);
esp_err_t esp_display_present_engine_get_caps(esp_display_present_engine_t *presenter, esp_display_present_backend_caps_t *caps);

/* Pump the backend when needed. A definite TE rejection may retry on a later edge. */
esp_err_t esp_display_present_engine_service(esp_display_present_engine_t *presenter);
/* Reap completed receipts and report lifecycle state, without granting leases. */
esp_err_t esp_display_present_engine_get_status(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *status);

/* Begin waits for pending work within the configured budget. A timeout leaves
 * the previous frame intact and creates no new frame. */
esp_err_t esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *presenter,
                                                       const esp_display_present_frame_request_t *request);
esp_err_t esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *presenter, esp_display_present_lease_t *lease);
/* Resolve the rows that fit for a given stride in the current unconsumed lease. */
esp_err_t esp_display_present_engine_resolve_rows(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                                  size_t stride_bytes, uint32_t requested_rows, uint32_t *out_rows);
esp_err_t esp_display_present_engine_submit_buffer(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                                   const esp_display_present_engine_area_t *area, size_t stride_bytes);
/* Seal the build. Optional declared coverage supplements actual write tracking. */
esp_err_t esp_display_present_engine_commit_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage);
/* Cancellation timeout keeps the frame context for retry. No active frame is OK. */
esp_err_t esp_display_present_engine_cancel_frame(esp_display_present_engine_t *presenter);

/* Resolve logical damage against the physical panel's region constraints before drawing. */
esp_err_t esp_display_present_engine_resolve_render_area(esp_display_present_engine_t *presenter,
                                                         const esp_display_present_rect_t *area, esp_display_present_rect_t *out);
esp_err_t esp_display_present_engine_get_rotation(esp_display_present_engine_t *presenter, esp_display_present_engine_rotation_t *out);
/* Drain then change the logical orientation with no open frame or lease.
 * Detach the renderer first and recreate it with the updated caps and a full redraw.
 * The physical panel size and panel MADCTL are unchanged. */
esp_err_t esp_display_present_engine_set_rotation(esp_display_present_engine_t *presenter,
                                                   esp_display_present_engine_rotation_t rotation, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
