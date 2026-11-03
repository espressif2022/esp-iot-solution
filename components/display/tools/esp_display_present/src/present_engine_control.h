/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "present_presenter.h"
/* Private facade bridge. Serializes through the presenter gate. Observation is
 * task-neutral; stop/destroy require the producer to have exited all task APIs.
 * This is not permission for concurrent producers or concurrent destruction. */
esp_err_t present_presenter_control_get_status(esp_display_present_engine_t *, esp_display_present_engine_status_t *);
/* valid distinguishes a filled snapshot reporting a runtime error from lock/argument failure. */
esp_err_t present_presenter_control_get_status_snapshot(esp_display_present_engine_t *, esp_display_present_engine_status_t *, bool *valid);
esp_err_t present_presenter_control_query_frame(esp_display_present_engine_t *, esp_display_present_engine_receipt_t, esp_display_present_frame_status_t *);
esp_err_t present_presenter_control_release(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);
esp_err_t present_presenter_control_stop(esp_display_present_engine_t *, uint32_t);
esp_err_t present_presenter_control_destroy(esp_display_present_engine_t *);

/* Owner-only admission with a remaining facade budget. */
esp_err_t present_presenter_begin_budget(esp_display_present_engine_t *, const esp_display_present_frame_request_t *, uint32_t);

esp_err_t present_presenter_control_cancel_tracking(esp_display_present_engine_t *, esp_display_present_engine_receipt_t);

/* Owner-only start with a remaining budget. The facade control finish entry
 * requires its logical frame/lease to have been revoked under its session gate;
 * no live producer may still access that frame. It may start native cancellation
 * if the first zero-budget attempt could not obtain the native gate. */
esp_err_t present_presenter_cancel_budget(esp_display_present_engine_t *, uint32_t);
esp_err_t present_presenter_control_finish_cancel(esp_display_present_engine_t *, uint32_t);

#ifdef PRESENT_HOST
/* Test fixture only: arrange deterministic competition with the core gate. */
void present_presenter_test_gate_lock(esp_display_present_engine_t *);
void present_presenter_test_gate_unlock(esp_display_present_engine_t *);
#endif
