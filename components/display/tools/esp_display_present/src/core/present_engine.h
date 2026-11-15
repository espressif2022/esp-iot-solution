/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "config/present_engine_config.h"
#include "core/present_engine_types.h"
#include "esp_display_present_lvgl_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Edge-only ingress, allowed from independent event tasks / ISRs respectively.
 * timestamp_us uses the backend monotonic clock domain. region ignores edges.
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
 * @brief Non-destructive: no active build; waits receipts using one total budget.
 * Returns the sticky accepted-operation error after drain, if any.
 *
 * @param presenter Presenter owned by the calling task.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_engine_drain(esp_display_present_engine_t *presenter, uint32_t timeout_ms);

/**
 * @brief Stops/joins backend worker before backend quiesce, using one timeout budget.
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

#ifdef __cplusplus
}
#endif
