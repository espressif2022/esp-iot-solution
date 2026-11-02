/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "present_presenter.h"
/* Private facade bridge. Serializes through the presenter gate. Observation is
 * task-neutral; stop/destroy require the producer to have exited all task APIs.
 * This is not permission for concurrent producers or concurrent destruction. */
esp_err_t pv2_presenter_control_get_status(pv2_presenter_t *, pv2_presenter_status_t *);
esp_err_t pv2_presenter_control_query_frame(pv2_presenter_t *, pv2_presenter_receipt_t, pv2_frame_status_t *);
esp_err_t pv2_presenter_control_release(pv2_presenter_t *, pv2_presenter_receipt_t);
esp_err_t pv2_presenter_control_stop(pv2_presenter_t *, uint32_t);
esp_err_t pv2_presenter_control_destroy(pv2_presenter_t *);

/* Owner-only admission with a remaining facade budget. */
esp_err_t pv2_presenter_begin_budget(pv2_presenter_t *, const pv2_frame_request_t *, uint32_t);

esp_err_t pv2_presenter_control_cancel_tracking(pv2_presenter_t *, pv2_presenter_receipt_t);

/* Owner-only start with a remaining budget. The facade control finish entry
 * requires its logical frame/lease to have been revoked under its session gate;
 * no live producer may still access that frame. It may start native cancellation
 * if the first zero-budget attempt could not obtain the native gate. */
esp_err_t pv2_presenter_cancel_budget(pv2_presenter_t *, uint32_t);
esp_err_t pv2_presenter_control_finish_cancel(pv2_presenter_t *, uint32_t);
