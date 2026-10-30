/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/** Experimental backend API; existing presenter API is not replaced. */
#include "present_pixel_engine.h"
#include "present_te_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pv2_backend pv2_backend_t;
typedef struct pv2_backend_config pv2_backend_config_t;
typedef struct {
    uint64_t value;
} pv2_frame_t;
typedef struct {
    uint64_t value;
} pv2_lease_id_t;
typedef struct {
    uint64_t value;
} pv2_receipt_t;

typedef enum {
    PV2_CONTRACT_PARTITION,
    PV2_CONTRACT_FULL,
    PV2_CONTRACT_DIRECT,
} pv2_render_contract_t;

typedef struct {
    pv2_render_contract_t contract;
    pv2_layout_t draw_layout;
    size_t drawbuf_capacity_bytes; /* Per-slot writable capacity in bytes. */
    size_t buffer_alignment;       /* Power-of-two base alignment in bytes. */
    uint32_t max_tile_rows;
    uint32_t max_damage_areas;
    bool retains_content;
    bool supports_present_confirmation;
    bool writes_active_scanout; /* Explicit persistent single-FB contract. */
} pv2_backend_caps_t;

typedef struct {
    const pv2_rect_t *damage;
    size_t damage_count;
    bool full_redraw;
} pv2_frame_request_t;

typedef struct {
    pv2_lease_id_t id;
    pv2_surface_t surface;
    pv2_rect_t logical_area; /* Surface origin maps to logical_area origin. */
} pv2_lease_t;

/** Optional override when submitting a leased buffer.
 * - logical_area {0,0,0,0} uses the lease's acquired logical_area.
 * - non-zero logical_area must fit within the lease's capacity and screen
 *   bounds (supports acquire_buffer without a pre-chosen sub-rect).
 *   x/y are SCREEN coordinates. Pixel (0,0) always starts at buffer.pixels;
 *   this is not a crop of an already rendered larger surface for PARTITION.
 *   FB FULL/DIRECT instead use screen x/y as offsets in the full leased surface;
 *   their stride must equal the full surface stride.
 * - stride_bytes == 0 uses the tight row stride (logical_area.width *bpp) for
 *   PARTITION or the lease's surface.layout.stride_bytes for FULL/DIRECT. */
typedef struct {
    pv2_rect_t logical_area;
    size_t stride_bytes;
} pv2_region_submit_t;

/** Optional explicit dirty coverage when sealing a FULL/DIRECT frame.
 * Ignored by GRAM (which tracks each submitted partition region directly). */
typedef struct {
    const pv2_rect_t *areas;
    size_t area_count;
    bool full_coverage;
} pv2_frame_coverage_t;

typedef enum {
    PV2_COMPLETION_SOURCE_RELEASED,
    PV2_COMPLETION_PRESENT_CONFIRMED,
} pv2_completion_point_t;

typedef struct {
    bool sealed;
    bool source_released;
    bool present_confirmed;
    bool terminal; /* No further progress for this frame receipt. */
    esp_err_t result;
} pv2_frame_status_t;

/** Sticky first accepted-operation failure, retained until backend destruction,
 * including fire-and-forget frames. ESP_OK means no failure observed. */
typedef struct {
    esp_err_t result;
    pv2_frame_t frame;
    uint64_t transaction_cookie;
} pv2_backend_fault_t;

/** Adapter-normalized event. Platforms must define cookie correlation, event
 * ordering, and the precise buffer-retirement meaning. A raw VSYNC is not
 * automatically a scanout retirement event. */
typedef enum {
    PV2_EVENT_TRANSFER_DONE,
    PV2_EVENT_SCANOUT_CONFIRMED,
    PV2_EVENT_TE_EDGE,
    PV2_EVENT_FAULT,
} pv2_event_kind_t;

typedef struct {
    pv2_event_kind_t kind;
    uint64_t transaction_cookie;
    uint64_t timestamp_us;
    esp_err_t result;
} pv2_backend_event_t;

/** Driver adapter: rejected means no access and no callback; accepted/uncertain
 * consumes the source until completion or successful quiesce. */
typedef enum {
    PV2_TX_REJECTED,
    PV2_TX_ACCEPTED,
    PV2_TX_UNCERTAIN,
} pv2_tx_acceptance_t;

typedef struct {
    pv2_tx_acceptance_t acceptance;
    esp_err_t result;
} pv2_tx_result_t;

typedef bool (*pv2_panel_event_fn)(void *user, const pv2_backend_event_t *event, bool from_isr);

typedef struct {
    void *context;
    /**
     * @brief Failure guarantees no callback registration remains.
     *
     * @param context Borrowed implementation context.
     * @param callback Completion callback, retained until successful quiesce.
     * @param user Borrowed callback context.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*attach)(void *context, pv2_panel_event_fn callback, void *user);
    /**
     * @brief Descriptor valid during call. GRAM/TE pixels until completion; FB pixels
     * through continuous scanout until confirmed replacement or quiesce.
     * Contract for GRAM/IO panels: surface must be tightly packed
     * (surface->layout.stride_bytes == area->width *bpp).
     * Adapter performs peripheral DMA cache writeback on the tx buffer,
     * associates cookies in strict FIFO/ID order, and may invoke completion
     * before submit returns.
     *
     * @param context Borrowed implementation context.
     * @param surface Borrowed pixel surface descriptor.
     * @param area Input rectangle; see the coordinate and ownership contract above.
     * @param cookie Unique hardware transaction cookie.
     * @return Acceptance and result; uncertain access remains borrowed.
     */
    pv2_tx_result_t (*submit)(void *context, const pv2_surface_t *surface, const pv2_rect_t *area, uint64_t cookie);
    /**
     * @brief Success guarantees no hardware accesses and no active/future callbacks.
     * Failure leaves registration/resources valid for retry. Must honor total
     * timeout budget; may safely abort hardware rather than finish display.
     *
     * @param context Borrowed implementation context.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*quiesce)(void *context, uint32_t timeout_ms);
} pv2_panel_adapter_t;

typedef bool (*pv2_progress_fn)(void *user, bool from_isr);

struct pv2_backend_config {
    uint32_t width, height;     /* Logical dimensions seen by the producer. */
    uint32_t tile_rows;         /* GRAM/TE partition height in rows. */
    size_t buffer_count;        /* 1..16 draw/frame buffers. */
    size_t max_frames;          /* 1..32 bounded frame/receipt records. */
    pv2_pixel_format_t format;
    pv2_byte_order_t draw_order;
    pv2_byte_order_t panel_order;
    pv2_rotation_t rotation;    /* Logical-to-physical clockwise rotation. */
    bool in_place_convert;      /* True: swap byte order in-place on tight stride and revert on rejection. */
    void *const *external_buffers; /* Optional caller/driver-owned FB array (TE/FB). */
    void *initial_scanout_buffer; /* Optional member of external_buffers already scanning; not writable until retired. */
    size_t external_buffer_bytes;  /* Capacity of each external buffer. */
    pv2_panel_adapter_t panel;  /* Borrowed until successful stop. */
    /** TE only: measured safe window from edge and worst-case whole-frame
     * transfer budget (including dispatch overhead). Both nonzero, budget < window.
     * GRAM ignores these fields. No hardware timing is inferred automatically. */
    uint32_t te_window_us;
    uint32_t te_transfer_budget_us;
    /** FB: 1..3 framebuffers in buffer_count, one scratch tile for PARTITION.
     * FULL/DIRECT leases expose the whole candidate; submit coordinates index
     * that full surface (not scratch origin). Zero defaults to PARTITION. */
    pv2_render_contract_t render_contract;
    /** Single-FB is permitted only with explicit acceptance of active scanout
     * writes/tearing. Cancel cannot roll back pixels already visible. */
    bool allow_active_scanout_writes;
    /** GRAM/TE and FB PARTITION queued tile copies. Defaults retain synchronous CPU operation.
     * Executor must obey pixel_execute's return-means-access-ended contract.
     * FB FULL/DIRECT reject async_copy; those contracts draw directly.
     * GRAM async_copy requires separate conversion storage (no in_place_convert). */
    bool async_copy;
    pv2_pixel_execute_fn copy_execute;
    void *copy_user;
    /** Optional wake only, outside locks; cannot reenter backend task APIs.
     * Caller context remains valid through successful stop. */
    pv2_progress_fn on_progress;
    void *progress_user;
    bool tile_in_psram, image_in_psram, logical_in_psram;
    size_t transfer_capacity; /* GRAM: 0 uses tile count. */
    pv2_te_timing_t te_timing; /* period_us=0 retains the explicit fixed edge window. */
    size_t tile_buffer_count; /* FB PARTIAL: independent draw pool; 0 legacy one. */
    size_t compose_count; /* TE: 0 legacy serialized dual-image, explicit 1/2 enables pool scheduling. */
};

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
    esp_err_t (*create)(const pv2_backend_config_t *config, pv2_backend_t **out_backend);
    /**
     * @brief Get caps.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param caps Receives the backend capabilities and current layout.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*get_caps)(pv2_backend_t *backend, pv2_backend_caps_t *caps);

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
    esp_err_t (*begin)(pv2_backend_t *backend, const pv2_frame_request_t *request, pv2_frame_t *frame);
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
    esp_err_t (*acquire)(pv2_backend_t *backend, pv2_frame_t frame, const pv2_rect_t *area, uint32_t timeout_ms, pv2_lease_t *lease);
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
    esp_err_t (*get_draw_buffers)(pv2_backend_t *backend, void **out_buffers, size_t max_buffers, size_t *out_count, size_t *out_buffer_bytes);
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
    esp_err_t (*submit_region)(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease_id);
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
    esp_err_t (*submit_region_ex)(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease_id, const pv2_region_submit_t *submit);
    /**
     * @brief Cancel region.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param frame Instance-scoped frame token.
     * @param lease_id Current unconsumed lease token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*cancel_region)(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease_id);
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
    esp_err_t (*end)(pv2_backend_t *backend, pv2_frame_t frame, pv2_receipt_t *out_receipt);
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
    esp_err_t (*end_ex)(pv2_backend_t *backend, pv2_frame_t frame, const pv2_frame_coverage_t *coverage, pv2_receipt_t *out_receipt);
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
    esp_err_t (*cancel_frame)(pv2_backend_t *backend, pv2_frame_t frame, uint32_t timeout_ms);

    /**
     * @brief Query.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param receipt Instance-scoped receipt token.
     * @param status Receives the observed state and operation result.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*query)(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_frame_status_t *status);
    /**
     * @brief Get fault.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param fault Receives the first accepted-operation failure.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*get_fault)(pv2_backend_t *backend, pv2_backend_fault_t *fault);
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
    esp_err_t (*wait)(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_completion_point_t point, uint32_t timeout_ms, pv2_frame_status_t *status);
    /**
     * @brief Terminal receipts only. Does not release a current scanout buffer.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param receipt Instance-scoped receipt token.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*release_receipt)(pv2_backend_t *backend, pv2_receipt_t receipt);

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
    bool (*on_event_isr)(pv2_backend_t *backend, const pv2_backend_event_t *event);
    /**
     * @brief On event.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param event Event descriptor, valid for this call.
     */
    void (*on_event)(pv2_backend_t *backend, const pv2_backend_event_t *event);
    /**
     * @brief Stop new work, drain accepted access, detach callbacks and wait active
     * ISR exit. On timeout keep object/resources valid, allowing retry.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*stop)(pv2_backend_t *backend, uint32_t timeout_ms);
    /**
     * @brief Requires QUIESCENT and no external receipts.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*destroy)(pv2_backend_t *backend);
    /**
     * @brief Task-only, no active build or pending receipts; physical size unchanged.
     *
     * @param backend Backend instance; task operations are caller-serialized.
     * @param rotation Clockwise logical-to-physical orientation.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*set_rotation)(pv2_backend_t *backend, pv2_rotation_t rotation);
} pv2_backend_ops_t;

/** One descriptor per complete backend, not independently paired stage and
 * lifecycle tables. All operations are required; unsupported capabilities
 * are declared by get_caps and rejected explicitly. */
extern const pv2_backend_ops_t pv2_gram_backend;
extern const pv2_backend_ops_t pv2_te_backend;

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
esp_err_t pv2_gram_service(pv2_backend_t *backend);

/**
 * @brief Te service.
 *
 * @param backend Backend instance; task operations are caller-serialized.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_te_service(pv2_backend_t *backend);
extern const pv2_backend_ops_t pv2_fb_backend;

/**
 * @brief Fb service.
 *
 * @param backend Backend instance; task operations are caller-serialized.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_fb_service(pv2_backend_t *backend);

/** FB adapters borrow the current surface continuously until a successful
 * SCANOUT_CONFIRMED for its replacement proves BOTH new activation and old
 * retirement, or quiesce succeeds. TRANSFER_DONE/raw VSYNC is insufficient.
 * Completion receipt releases producer access, never the scanout allocation;
 * only a new lease grants write permission. External buffers must be exclusively
 * handed off, 64-byte aligned, disjoint, and live until successful stop. */

#ifdef __cplusplus
}
#endif
