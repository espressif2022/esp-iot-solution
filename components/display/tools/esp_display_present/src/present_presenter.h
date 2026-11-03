/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_backend.h"
#include "present_output_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_display_present_engine esp_display_present_engine_t;
typedef struct {
    int32_t x1, y1, x2, y2;
} esp_display_present_engine_area_t; /* Inclusive, v1-style. */
typedef enum { ESP_DISPLAY_PRESENT_ENGINE_GRAM = 0, ESP_DISPLAY_PRESENT_ENGINE_TE = 1, ESP_DISPLAY_PRESENT_ENGINE_FB = 2 } esp_display_present_engine_backend_t;
typedef enum {
    ESP_DISPLAY_PRESENT_MODE_DEFAULT = 0, /* Use backend_kind/config verbatim; source compatibility. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_FULL,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_PARTIAL,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL,
    ESP_DISPLAY_PRESENT_ENGINE_MODE_AUTO,
} esp_display_present_present_mode_t;
typedef struct {
    esp_display_present_backend_config_t backend; /* GRAM/TE PARTITION or FB PARTITION/FULL/DIRECT. */
    uint32_t timeout_ms; /* 0 polls; UINT32_MAX waits indefinitely. */
    esp_display_present_engine_backend_t backend_kind; /* Zero preserves GRAM default. */
    esp_display_present_present_mode_t mode;
    /** Optional sender worker; default false retains cooperative service.
     * GRAM requires backend.async_copy=true for background_service.
     * Host uses pthread; IDF defaults: 4096-byte stack, priority 5. */
    bool background_service;
    uint32_t worker_stack_bytes; /* 0 selects default; ignored by Host. */
    unsigned worker_priority; /* 0 selects default; ignored by Host. */
    /** FB named modes require backend_kind FB. NONE selects a single FB and
     * requires backend.allow_active_scanout_writes; IO NONE selects GRAM.
     * AUTO uses explicit backend kind (no hardware detection): FB 2/3 buffers
     * -> PARTITION, FB 1 -> NONE; IO with both TE budgets -> TE, otherwise GRAM.
     * Named FB modes set buffer_count; external array must have that many entries. */
    esp_display_present_output_caps_t output_caps; /* Zero region alignments mean 1. */
    /** Per physical panel.submit completion deadline, including driver call
     * and callback dispatch. Zero disables (legacy V2 behavior). Timeout is a
     * sticky fault, never synthetic completion or permission to free pixels.
     * Checked by service/task APIs and completion ingress; an existing sender
     * wakes at the next deadline. No extra task; a blocking driver call cannot
     * be interrupted. Not a deadline for drawing, copy, repair or waiting TE. */
    uint32_t transfer_timeout_ms;
} esp_display_present_engine_config_t;

typedef enum { ESP_DISPLAY_PRESENT_ENGINE_RUNNING, ESP_DISPLAY_PRESENT_ENGINE_CLOSING, ESP_DISPLAY_PRESENT_ENGINE_STOPPED } esp_display_present_engine_state_t;
typedef struct {
    esp_display_present_engine_state_t state;
    bool frame_active;
    bool cancel_pending;
    size_t pending_frames;
    uint64_t committed_frames;
    uint64_t completed_frames; /* Terminal transfers, not necessarily successful. */
    esp_err_t first_error; /* Sticky accepted-operation error, not a transient rejection. */
    esp_err_t last_service_error; /* Latest pump result, including retryable rejection. */
} esp_display_present_engine_status_t;

/**
 * @brief Owns its backend. All task APIs, including getters and destroy, must run on
 * the creating/bound task, except TE notifications and explicit producer handoff.
 * Does not own panel/IO.
 * This is a source-migration facade, NOT a binary-compatible v1 replacement.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_presenter Output presenter.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_create(const esp_display_present_engine_config_t *config, esp_display_present_engine_t **out_presenter);

/**
 * @brief New owner calls only after the old owner has stopped ALL task API calls.
 * Rejects open builds/cancellation; sealed pending frames continue normally.
 * Does not transfer an LVGL binding (LVGL task/locking remains caller-owned).
 *
 * @param presenter Presenter owned by the calling task.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_rebind_producer(esp_display_present_engine_t *presenter, uint32_t timeout_ms);

/**
 * @brief Pool inspection, not write permission. Only an acquired lease permits writes;
 * addresses cannot be registered with a renderer that draws before acquire.
 *
 * @param presenter Presenter owned by the calling task.
 * @param out_buffers Output buffers.
 * @param capacity Number of entries available in out_buffers.
 * @param count Receives the buffer count.
 * @param bytes Receives the writable capacity per buffer in bytes.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_get_draw_buffers(esp_display_present_engine_t *presenter, void **out_buffers, size_t capacity, size_t *count, size_t *bytes);

/**
 * @brief Owner-task pump (optional when background_service is enabled).
 * TE/FB send/retire frames; GRAM reaps receipts. A definite
 * rejection is retryable on a later TE edge and is not a sticky fault.
 *
 * @param presenter Presenter owned by the calling task.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_service(esp_display_present_engine_t *presenter);

/**
 * @brief Edge-only ingress, allowed from independent event tasks / ISRs respectively.
 * timestamp_us uses the backend monotonic clock domain. GRAM ignores edges.
 * These calls never send. Before destroy, detach the external event source AND
 * join all its callbacks. Neither function reads mutable owner-task state.
 *
 * @param presenter Presenter owned by the calling task.
 * @param timestamp_us Timestamp in the backend monotonic clock domain.
 */
void esp_display_present_engine_notify_te(esp_display_present_engine_t *presenter, uint64_t timestamp_us);

/**
 * @brief Presenter notify te isr.
 *
 * @param presenter Presenter owned by the calling task.
 * @param timestamp_us Timestamp in the backend monotonic clock domain.
 * @return True to request an ISR yield; see the callback contract above.
 */
bool esp_display_present_engine_notify_te_isr(esp_display_present_engine_t *presenter, uint64_t timestamp_us);

/**
 * @brief Presenter get caps.
 *
 * @param presenter Presenter owned by the calling task.
 * @param caps Receives the backend capabilities and current layout.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_get_caps(esp_display_present_engine_t *presenter, esp_display_present_backend_caps_t *caps);

/**
 * @brief Drain then change orientation while keeping physical panel size fixed.
 * No open frame/lease. Caller must detach its renderer (including LVGL binding)
 * first, then recreate it with updated caps and request a full redraw.
 * This does not change panel MADCTL or automatically reconfigure LVGL.
 *
 * @param presenter Presenter owned by the calling task.
 * @param rotation Clockwise logical-to-physical orientation.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_set_rotation(esp_display_present_engine_t *presenter, esp_display_present_engine_rotation_t rotation, uint32_t timeout_ms);

/**
 * @brief Pending frames are waited within one timeout_ms budget. Timeout leaves the
 * previous frame intact and creates no new frame; it is not a sticky fault.
 *
 * @param presenter Presenter owned by the calling task.
 * @param request Request descriptor, subject to the ownership contract above.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_request_t *request);

/**
 * @brief Presenter acquire buffer.
 *
 * @param presenter Presenter owned by the calling task.
 * @param lease Lease descriptor; only an active lease grants write permission.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *presenter, esp_display_present_lease_t *lease);

/**
 * @brief Only valid for the current unconsumed lease. Does not change its layout.
 *
 * @param presenter Presenter owned by the calling task.
 * @param lease Lease descriptor; only an active lease grants write permission.
 * @param stride_bytes Row stride in bytes.
 * @param requested_rows Maximum rows requested by the producer.
 * @param out_rows Output rows.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_resolve_rows(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease, size_t stride_bytes, uint32_t requested_rows, uint32_t *out_rows);

/**
 * @brief Presenter submit buffer.
 *
 * @param presenter Presenter owned by the calling task.
 * @param lease Lease descriptor; only an active lease grants write permission.
 * @param area Input rectangle; see the coordinate and ownership contract above.
 * @param stride_bytes Row stride in bytes.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_submit_buffer(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease, const esp_display_present_engine_area_t *area, size_t stride_bytes);

/**
 * @brief Facade retains a bounded backend receipt until terminal completion.
 *
 * @param presenter Presenter owned by the calling task.
 * @param coverage Optional declared coverage; does not replace actual write tracking.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_commit_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage);

/** Optional, bounded observer records (32 per presenter), independent of backend
 * buffer ownership. Tokens are instance-scoped; callers explicitly release a
 * terminal observer or cancel tracking. Untracked commit remains unchanged. */
typedef struct {
    uint64_t value;
} esp_display_present_engine_receipt_t;

/**
 * @brief Presenter commit with receipt.
 *
 * @param presenter Presenter owned by the calling task.
 * @param coverage Optional declared coverage; does not replace actual write tracking.
 * @param receipt Instance-scoped receipt token.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_commit_with_receipt(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage, esp_display_present_engine_receipt_t *receipt);

/**
 * @brief Presenter query frame.
 *
 * @param presenter Presenter owned by the calling task.
 * @param receipt Instance-scoped receipt token.
 * @param status Receives the observed state and operation result.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_query_frame(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt, esp_display_present_frame_status_t *status);

/**
 * @brief Presenter wait frame.
 *
 * @param presenter Presenter owned by the calling task.
 * @param receipt Instance-scoped receipt token.
 * @param point Requested completion guarantee.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @param status Receives the observed state and operation result.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_wait_frame(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt, esp_display_present_completion_point_t point, uint32_t timeout_ms, esp_display_present_frame_status_t *status);

/**
 * @brief Presenter release receipt.
 *
 * @param presenter Presenter owned by the calling task.
 * @param receipt Instance-scoped receipt token.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_release_receipt(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt);

/**
 * @brief Releases observation only, never cancels or reclaims an in-flight transfer.
 * Invalid/stale tokens are rejected. Destroy requires all observers released.
 *
 * @param presenter Presenter owned by the calling task.
 * @param receipt Instance-scoped receipt token.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_cancel_tracking(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt);

/**
 * @brief Timeout keeps cancellation context for retry. No active frame => ESP_OK.
 *
 * @param presenter Presenter owned by the calling task.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_cancel_frame(esp_display_present_engine_t *presenter);

/**
 * @brief Non-destructive: no active build; waits receipts using one total budget.
 * Returns the sticky accepted-operation error after drain, if any.
 *
 * @param presenter Presenter owned by the calling task.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_drain(esp_display_present_engine_t *presenter, uint32_t timeout_ms);

/**
 * @brief Reaps completed receipts; cannot turn unknown hardware state into free memory.
 *
 * @param presenter Presenter owned by the calling task.
 * @param status Receives the observed state and operation result.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_get_status(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *status);

/**
 * @brief Stops/joins sender before backend quiesce, using one timeout budget.
 * Timeout preserves the closing object, worker and backend for retry.
 * Success establishes resource safety even when first_error remains nonzero.
 *
 * @param presenter Presenter owned by the calling task.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_stop(esp_display_present_engine_t *presenter, uint32_t timeout_ms);

/**
 * @brief Presenter destroy.
 *
 * @param presenter Presenter owned by the calling task.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_destroy(esp_display_present_engine_t *presenter);

esp_err_t esp_display_present_engine_get_rotation(esp_display_present_engine_t *, esp_display_present_engine_rotation_t *);
/* Before rendering; expands logical damage to satisfy physical panel regions. */
esp_err_t esp_display_present_engine_resolve_render_area(esp_display_present_engine_t *, const esp_display_present_rect_t *, esp_display_present_rect_t *);

struct esp_display_presenter;
/* Exclusive renderer session handoff for bindings such as esp_lv_present. */
esp_err_t present_session_bind(struct esp_display_presenter *presenter, esp_display_present_engine_t **out_engine);
void present_session_unbind(struct esp_display_presenter *presenter);
uint64_t present_session_committed(struct esp_display_presenter *presenter);

esp_err_t present_presenter_control_get_status(esp_display_present_engine_t *, esp_display_present_engine_status_t *);
esp_err_t present_presenter_control_get_status_snapshot(esp_display_present_engine_t *, esp_display_present_engine_status_t *, bool *valid);
esp_err_t present_presenter_control_query_frame(esp_display_present_engine_t *, esp_display_present_engine_receipt_t, esp_display_present_frame_status_t *);
esp_err_t present_presenter_control_release(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);
esp_err_t present_presenter_control_stop(esp_display_present_engine_t *, uint32_t);
esp_err_t present_presenter_control_destroy(esp_display_present_engine_t *);
esp_err_t present_presenter_begin_budget(esp_display_present_engine_t *, const esp_display_present_frame_request_t *, uint32_t);
esp_err_t present_presenter_control_cancel_tracking(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);
esp_err_t present_presenter_cancel_budget(esp_display_present_engine_t *, uint32_t);
esp_err_t present_presenter_control_finish_cancel(esp_display_present_engine_t *, uint32_t);

#ifdef PRESENT_HOST
void present_presenter_test_gate_lock(esp_display_present_engine_t *);
void present_presenter_test_gate_unlock(esp_display_present_engine_t *);
#endif

#ifdef __cplusplus
}
#endif
