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

/**
 * @brief Owner-neutral facade control operations.
 *
 * Stop/destroy require the producer to be paused; status/receipt operations
 * retain the engine's existing serialization without granting frame/lease access.
 */

/** @brief Drain and bind the calling task as the new engine producer without canceling transfers. */
esp_err_t present_presenter_control_bind_producer(esp_display_present_engine_t *, uint32_t);
/** @brief Poll a stopped producer's receipts without executing driver submissions and release tracked observer. */
esp_err_t present_presenter_control_poll_quiesce(esp_display_present_engine_t *, uint32_t,
                                                 esp_display_present_engine_receipt_t,
                                                 esp_display_present_engine_status_t *,
                                                 esp_display_present_frame_status_t *);
/** @brief Service backend status and snapshot engine status. */
esp_err_t present_presenter_control_get_status(esp_display_present_engine_t *, esp_display_present_engine_status_t *);
/** @brief Snapshot engine status without driving backend submissions. */
esp_err_t present_presenter_control_get_status_snapshot(esp_display_present_engine_t *, esp_display_present_engine_status_t *, bool *valid);
/** @brief Query a tracked receipt's frame completion status. */
esp_err_t present_presenter_control_query_frame(esp_display_present_engine_t *, esp_display_present_engine_receipt_t, esp_display_present_frame_status_t *);
/** @brief Release a tracked receipt observer without blocking. */
esp_err_t present_presenter_control_release(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);
/** @brief Stop the presentation engine and drain pending work within timeout. */
esp_err_t present_presenter_control_stop(esp_display_present_engine_t *, uint32_t);
/** @brief Destroy a stopped presentation engine. */
esp_err_t present_presenter_control_destroy(esp_display_present_engine_t *);
/** @brief Cancel tracking observation for a receipt without releasing hardware transfers. */
esp_err_t present_presenter_control_cancel_tracking(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);
/** @brief Finish draining a pending pre-commit cancellation within timeout. */
esp_err_t present_presenter_control_finish_cancel(esp_display_present_engine_t *, uint32_t);

/** @brief Enable and reset opt-in performance metrics counters. */
esp_err_t present_presenter_control_reset_metrics(esp_display_present_engine_t *);
/** @brief Read opt-in performance metrics counters. */
esp_err_t present_presenter_control_get_metrics(esp_display_present_engine_t *, esp_display_presenter_metrics_t *);
/** @brief Record an observed hardware VSYNC pulse for scanout-miss metrics. */
void present_presenter_metrics_vsync(esp_display_present_engine_t *, bool isr);

#ifdef PRESENT_HOST
void present_presenter_test_gate_lock(esp_display_present_engine_t *);
void present_presenter_test_gate_unlock(esp_display_present_engine_t *);
#endif

#ifdef __cplusplus
}
#endif
