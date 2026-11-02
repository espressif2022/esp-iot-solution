/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_pixel_engine.h"

/* Internal composition core, not a panel backend. All calls are serialized
 * task-context calls. TE owns leases, receipts and panel event correlation.
 * write() drains its borrow; queue_write() retains it until finish_write(). */
typedef struct pv2_te_composer pv2_te_composer_t;
typedef struct {
    uint64_t value;
} pv2_te_frame_t;
typedef struct {
    uint32_t width, height;
    bool image_in_psram;
    size_t image_count; /* 0 -> 2; explicit 1 or 2. */
    pv2_pixel_format_t format;
    pv2_byte_order_t panel_order;
    pv2_rotation_t rotation; /* Applied to producer writes only, not repair. */
    pv2_pixel_config_t pixel; /* max_jobs=0 selects synchronous default. */
} pv2_te_composer_config_t;

esp_err_t pv2_te_composer_create(const pv2_te_composer_config_t *, pv2_te_composer_t **);
/* One construction writer, with up to two READY/INFLIGHT image slots. Explicit
 * image_count publishes the content baseline at READY; legacy zero serializes
 * frames and publishes only on successful transfer. Tokens reject stale reuse. */
esp_err_t pv2_te_composer_begin(pv2_te_composer_t *, pv2_te_frame_t *);
esp_err_t pv2_te_composer_write(pv2_te_composer_t *, pv2_te_frame_t,
                                const pv2_source_t *, const pv2_rect_t *source_area,
                                uint32_t destination_x, uint32_t destination_y);
/* Tracks actual successful writes, not caller damage hints. First frame must
 * cover every pixel. Later frames repair only uncovered spans. An incomplete first frame stays BUILDING; asynchronous repair returns
 * TIMEOUT in REPAIR and must be polled by service until READY. ready_us uses the same monotonic clock as edges. */
esp_err_t pv2_te_composer_seal(pv2_te_composer_t *, pv2_te_frame_t, uint64_t ready_us);
/* Task-side admission: only a fresh edge at/after READY and within window_us.
 * Returns a read-only source, pinned until reject/complete. No panel operation.
 * Timeout leaves READY. Caller must guarantee available window fits its transfer;
 * this gate alone does not prove tear-free output. Timestamps must not wrap.
 * Call only for the latest observed edge; old edges are never queued as credits. */
esp_err_t pv2_te_composer_offer(pv2_te_composer_t *, pv2_te_frame_t,
                                uint64_t edge_us, uint64_t now_us, uint64_t window_us,
                                pv2_source_t *);
/* Only a DEFINITELY unaccepted transmission can return to READY. Retry waits
 * for a newer edge. ACCEPTED/UNCERTAIN must stay pinned until access ends. */
esp_err_t pv2_te_composer_reject(pv2_te_composer_t *, pv2_te_frame_t);
/* Call only after all peripheral reads end, including safe abort. Legacy zero
 * image_count publishes on success; explicit pools already published at READY.
 * On failure the backend must fault queued successors; content version does not
 * imply successful presentation. No display confirmation claim. */
esp_err_t pv2_te_composer_complete(pv2_te_composer_t *, pv2_te_frame_t, esp_err_t result);
/* BUILDING/REPAIR/READY only; pending repair can return TIMEOUT; INFLIGHT cannot be canceled or destroyed. */
esp_err_t pv2_te_composer_cancel(pv2_te_composer_t *, pv2_te_frame_t);
esp_err_t pv2_te_composer_destroy(pv2_te_composer_t *);

/* Queued writes retain source until finish_write succeeds (operation result is
 * separate). Seal/cancel refuse while tickets remain. All composer calls are
 * serialized; pixel notifications must only wake the owner, not call back here. */
esp_err_t pv2_te_composer_queue_write(pv2_te_composer_t *, pv2_te_frame_t,
                                      const pv2_source_t *, const pv2_rect_t *, uint32_t, uint32_t, pv2_copy_ticket_t *);
esp_err_t pv2_te_composer_finish_write(pv2_te_composer_t *, pv2_copy_ticket_t, esp_err_t *);
esp_err_t pv2_te_composer_stop(pv2_te_composer_t *, uint32_t);

esp_err_t pv2_te_composer_set_rotation(pv2_te_composer_t *, pv2_rotation_t);

/* Output policy has already established admission; no timing model here. */
esp_err_t pv2_te_composer_pin(pv2_te_composer_t *, pv2_te_frame_t, pv2_source_t *);

/* Drained topology-preserving reinterpretation, same pixel count. */
esp_err_t pv2_te_composer_reorient(pv2_te_composer_t *, pv2_rotation_t, uint32_t, uint32_t);
