/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Private backend virtual dispatch table.
 *
 * Configuration remains opaque here; include present_backend_config.h when
 * constructing or inspecting it.
 */
typedef struct {
    const char *name; /**< Backend identifier string. */
    esp_err_t (*create)(const esp_display_present_backend_config_t *config, present_backend_t **out_backend); /**< Create backend with default wake policy. */
    esp_err_t (*create_scheduled)(const esp_display_present_backend_config_t *config, present_backend_t **out_backend, bool autonomous); /**< Create backend with explicit engine background-service policy. */
    esp_err_t (*service)(present_backend_t *backend); /**< Task-context progress pump under the engine gate. */
    bool (*copy_work)(present_backend_t *backend); /**< Optional blocking copy step run on backend worker outside gate. */
    bool (*can_defer_service)(present_backend_t *backend); /**< Optional concurrent snapshot: construction-only work, no ready scanout/retirement/fault. */
    esp_err_t (*get_caps)(present_backend_t *backend, esp_display_present_backend_caps_t *caps); /**< Query backend capabilities. */
    esp_err_t (*begin)(present_backend_t *backend, const esp_display_present_frame_request_t *request, esp_display_present_frame_t *frame); /**< Begin a frame. */
    esp_err_t (*acquire)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_rect_t *area, uint32_t timeout_ms, esp_display_present_lease_t *lease); /**< Acquire a surface lease. */
    esp_err_t (*get_draw_buffers)(present_backend_t *backend, void **out_buffers, size_t max_buffers, size_t *out_count, size_t *out_buffer_bytes); /**< Inspect pool addresses. */
    esp_err_t (*submit_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id); /**< Submit leased region as acquired. */
    esp_err_t (*submit_region_ex)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id, const esp_display_present_region_submit_t *submit); /**< Submit leased region with override area/stride. */
    esp_err_t (*cancel_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id); /**< Cancel an unsubmitted region lease. */
    esp_err_t (*end)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_receipt_t *out_receipt); /**< Seal active frame. */
    esp_err_t (*end_ex)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_frame_coverage_t *coverage, esp_display_present_receipt_t *out_receipt); /**< Seal active frame with explicit coverage. */
    esp_err_t (*cancel_frame)(present_backend_t *backend, esp_display_present_frame_t frame, uint32_t timeout_ms); /**< Cancel active frame within timeout. */
    esp_err_t (*query)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_frame_status_t *status); /**< Query and advance receipt status. */
    esp_err_t (*peek)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_frame_status_t *status); /**< Inspect receipt without driving driver submissions. */
    esp_err_t (*forget)(present_backend_t *backend, esp_display_present_receipt_t receipt); /**< Drop receipt observer state. */
    esp_err_t (*get_fault)(present_backend_t *backend, esp_display_present_backend_fault_t *fault); /**< Read sticky backend fault. */
    esp_err_t (*wait)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_completion_point_t point, uint32_t timeout_ms, esp_display_present_frame_status_t *status); /**< Wait for a completion point. */
    esp_err_t (*release_receipt)(present_backend_t *backend, esp_display_present_receipt_t receipt); /**< Release a terminal receipt. */
    bool (*on_event_isr)(present_backend_t *backend, const esp_display_present_backend_event_t *event); /**< Panel ISR event ingress. */
    void (*on_event)(present_backend_t *backend, const esp_display_present_backend_event_t *event); /**< Panel task event ingress. */
    esp_err_t (*stop)(present_backend_t *backend, uint32_t timeout_ms); /**< Stop backend and quiesce hardware. */
    esp_err_t (*destroy)(present_backend_t *backend); /**< Destroy stopped backend. */
    esp_err_t (*set_rotation)(present_backend_t *backend, esp_display_present_engine_rotation_t rotation); /**< Reconfigure logical orientation. */
} present_backend_ops_t;

extern const present_backend_ops_t present_region_backend; /**< Partial-region panel-IO backend ops. */
extern const present_backend_ops_t present_te_backend;     /**< Tearing-synchronized compose backend ops. */
extern const present_backend_ops_t present_fb_backend;     /**< Framebuffer scanout backend ops. */

#ifdef __cplusplus
}
#endif
