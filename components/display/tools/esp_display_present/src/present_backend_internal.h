/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_backend.h"

typedef struct present_backend present_backend_t;

/** All task-side operations are serialized by a single caller, except pixel
 * engine operations which use their own lock. ISR/task callbacks may occur
 * concurrently. Destroy must never race any caller. Unsupported policies are rejected explicitly at create time. */
typedef struct {
    const char *name;
    /**
     * @brief Create.
     *
     * @param config Configuration; the pointer is not retained.
     * @param out_backend Output backend.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*create)(const esp_display_present_backend_config_t *config, present_backend_t **out_backend);
    /**
     * @brief Get caps.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param caps Receives the backend capabilities and current layout.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*get_caps)(present_backend_t *backend, esp_display_present_backend_caps_t *caps);

    /**
     * @brief Reserves a completion record before any transfer. Does not retain the
     * request pointer. GRAM validates damage hints but performs no repair;
     * the caller submits every region that needs updating.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param request Request descriptor, subject to the ownership contract above.
     * @param frame Instance-scoped frame token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*begin)(present_backend_t *backend, const esp_display_present_frame_request_t *request, esp_display_present_frame_t *frame);
    /**
     * @brief FB FULL/DIRECT always return the entire framebuffer; optional area is
     * validated but does not offset the pointer. Otherwise area may be NULL
     * to acquire a raw scratch buffer lease (defaulting
     * to {0, 0, width, min(height, tile_rows)}) whose exact sub-rectangle and
     * stride are supplied later via submit_region_ex. When area is non-NULL,
     * may return a smaller height due to tile capacity.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param area Input rectangle; see the coordinate and ownership contract above.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @param lease Lease descriptor; only an active lease grants write permission.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*acquire)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_rect_t *area, uint32_t timeout_ms, esp_display_present_lease_t *lease);
    /**
     * @brief Borrow underlying draw-buffer pointers for PARTITION static registration
     * (e.g. lv_display_set_buffers). These addresses grant NO write permission;
     * the renderer must hold the matching lease before drawing, not merely
     * acquire one after rendering. Active transfers still require begin /
     * acquire / submit_region / end lifecycle. Optional; returns
     * ESP_ERR_NOT_SUPPORTED when unavailable.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param out_buffers Output buffers.
     * @param max_buffers Number of available output pointer entries.
     * @param out_count Output count.
     * @param out_buffer_bytes Output buffer bytes.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*get_draw_buffers)(present_backend_t *backend, void **out_buffers, size_t max_buffers, size_t *out_count, size_t *out_buffer_bytes);
    /**
     * @brief ESP_OK consumes lease; non-OK preserves it and guarantees that this
     * submission has not been accepted. Async failure is reported via status
     * and blocks further acquire/submit on the faulted frame.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param lease_id Current unconsumed lease token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*submit_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id);
    /**
     * @brief Submit with explicit sub-area and/or custom row stride. Passing NULL
     * for submit is equivalent to submit_region().
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param lease_id Current unconsumed lease token.
     * @param submit Optional area and stride override.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*submit_region_ex)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id, const esp_display_present_region_submit_t *submit);
    /**
     * @brief Cancel region.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param lease_id Current unconsumed lease token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*cancel_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id);
    /**
     * @brief Requires no outstanding writable leases. Seals frame.
     * - If out_receipt != NULL: publishes an external receipt that the caller
     * MUST eventually recycle via release_receipt() once terminal.
     * - If out_receipt == NULL: fire-and-forget mode; the backend recycles the
     * frame record automatically as soon as all accepted work completes.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param out_receipt Output receipt.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*end)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_receipt_t *out_receipt);
    /**
     * @brief Seal frame with explicit dirty coverage (for FULL/DIRECT repair).
     * Passing NULL coverage is equivalent to end().
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param coverage Optional declared coverage; does not replace actual write tracking.
     * @param out_receipt Output receipt.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*end_ex)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_frame_coverage_t *coverage, esp_display_present_receipt_t *out_receipt);
    /**
     * @brief Closes frame and drains accepted operations. Timeout preserves closing
     * state; if the last in-flight operation later completes in ISR, the
     * canceled frame record auto-recycles so the backend is not wedged.
     * The most recently completed cancellation can be retried any number of
     * times until another cancellation completes (including synchronous cancel).
     * Unknown/older tokens are rejected; end() seals a frame against cancel.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*cancel_frame)(present_backend_t *backend, esp_display_present_frame_t frame, uint32_t timeout_ms);

    /**
     * @brief Query.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param receipt Instance-scoped receipt token.
     * @param status Receives the observed state and operation result.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*query)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_frame_status_t *status);
    /**
     * @brief Get fault.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param fault Receives the first accepted-operation failure.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*get_fault)(present_backend_t *backend, esp_display_present_backend_fault_t *fault);
    /**
     * @brief Unsupported present confirmation returns ESP_ERR_NOT_SUPPORTED.
     * ESP_OK returns a satisfied point or terminal failure; inspect status.
     * timeout_ms semantics match pixel_wait. Timeout never releases buffers.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param receipt Instance-scoped receipt token.
     * @param point Requested completion guarantee.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @param status Receives the observed state and operation result.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*wait)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_completion_point_t point, uint32_t timeout_ms, esp_display_present_frame_status_t *status);
    /**
     * @brief Terminal receipts only. Does not release a current scanout buffer.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param receipt Instance-scoped receipt token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*release_receipt)(present_backend_t *backend, esp_display_present_receipt_t receipt);

    /**
     * @brief ISR-safe and IRAM-safe where the platform requires. No allocation or
     * blocking; updates authoritative state under a short lock. True requests
     * yield. Task context events need a separate adapter dispatch, never invoke
     * FromISR primitives directly from a task.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param event Event descriptor, valid for this call.
     * @return True to request an ISR yield; see the callback contract above.
     */
    bool (*on_event_isr)(present_backend_t *backend, const esp_display_present_backend_event_t *event);
    /**
     * @brief On event.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param event Event descriptor, valid for this call.
     */
    void (*on_event)(present_backend_t *backend, const esp_display_present_backend_event_t *event);
    /**
     * @brief Stop new work, drain accepted access, detach callbacks and wait active
     * ISR exit. On timeout keep object/resources valid, allowing retry.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*stop)(present_backend_t *backend, uint32_t timeout_ms);
    /**
     * @brief Requires QUIESCENT and no external receipts.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*destroy)(present_backend_t *backend);
    /**
     * @brief Task-only, no active build or pending receipts; physical size unchanged.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param rotation Clockwise logical-to-physical orientation.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*set_rotation)(present_backend_t *backend, esp_display_present_engine_rotation_t rotation);
} present_backend_ops_t;

/** One descriptor per complete backend, not independently paired stage and
 * lifecycle tables. All operations are required; unsupported capabilities
 * are declared by get_caps and rejected explicitly. */
extern const present_backend_ops_t present_gram_backend;
extern const present_backend_ops_t present_te_backend;

/** TE CPU baseline is cooperatively serviced, not an autonomous worker.
 * Call from the same serialized producer as other task APIs; query/wait/begin
 * also service it. Fire-and-forget users MUST keep pumping this function.
 * on_event[_isr] only records TE edges/completions and never submits hardware.
 * Edge timestamps use the platform monotonic microsecond clock (ESP esp_timer).
 * Stop the external TE event producer and join its callbacks before destroy;
 * panel.quiesce covers only the events owned by that adapter.
 * A definite panel rejection preserves READY; service returns its error and
 * a later call can retry on a fresh edge. Accepted failures are sticky faults. */
/**
 * @brief GRAM async copy pump: definite panel rejection retains queued source and
 * returns a retryable error. Call service again; do not resubmit the lease.
 *
 * @param backend Backend instance; task operations are caller-serialized.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t present_gram_service(present_backend_t *backend);

/**
 * @brief Te service.
 *
 * @param backend Backend instance; task operations are caller-serialized.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
/* Manual mode can retain explicit service on SDKs without a drainable timer. */
esp_err_t present_te_create_scheduled(const esp_display_present_backend_config_t *, present_backend_t **, bool autonomous);
esp_err_t present_te_service(present_backend_t *backend);
extern const present_backend_ops_t present_fb_backend;

/**
 * @brief Fb service.
 *
 * @param backend Backend instance; task operations are caller-serialized.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t present_fb_service(present_backend_t *backend);
