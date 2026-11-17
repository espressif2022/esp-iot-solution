/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "core/present_engine_types.h"
#include "backend/present_backend.h"
#include "platform/present_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bounded per-receipt completion observer entry (accessed under engine gate). */
typedef struct {
    uint64_t token;                             /**< Instance-scoped observer token (0 is empty). */
    esp_display_present_receipt_t backend;      /**< Underlying backend frame receipt. */
    esp_display_present_frame_status_t status;  /**< Cached terminal frame status. */
} present_completion_observer_t;

/** @brief Bounded FIFO completion tracker for frame receipts and latched errors. */
typedef struct {
    esp_display_present_receipt_t receipts[PRESENT_MAX_FRAMES];         /**< Pending backend receipt ring. */
    present_completion_observer_t observers[PRESENT_MAX_FRAMES];        /**< Optional receipt observers. */
    size_t head, count, limit;                                          /**< Ring head, active entries, and capacity. */
    uint64_t committed, completed;                                      /**< Monotonic commit and completion counts. */
    esp_err_t first_error;                                              /**< Sticky first accepted-operation error. */
} present_completion_tracker_t;

/** @brief Latch the first backend fault in event order independently of FIFO reaping. */
void present_completion_record_error(present_completion_tracker_t *, esp_err_t);
/** @brief Reap completed backend receipts from the ring head and update counters/errors. */
esp_err_t present_completion_reap(present_completion_tracker_t *, const present_backend_ops_t *, present_backend_t *, bool service);
/** @brief Look up a tracked observer entry matching @p receipt. */
present_completion_observer_t *present_completion_find(present_completion_tracker_t *, esp_display_present_engine_receipt_t);
/** @brief Query live or cached completion status for a tracked observer. */
esp_err_t present_completion_query(present_completion_observer_t *, const present_backend_ops_t *, present_backend_t *, esp_display_present_frame_status_t *, bool service);

#ifdef __cplusplus
}
#endif
