/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/** Experimental backend API; existing presenter API is not replaced. */
#include "present_pixel_engine.h"
#include "platform/present_te_timing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_display_present_backend_config esp_display_present_backend_config_t;
typedef struct {
    uint64_t value;
} esp_display_present_frame_t;
typedef struct {
    uint64_t value;
} esp_display_present_lease_id_t;
typedef struct {
    uint64_t value;
} esp_display_present_receipt_t;

typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL,
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT,
} esp_display_present_render_contract_t;

typedef struct {
    esp_display_present_render_contract_t contract;
    esp_display_present_layout_t draw_layout;
    size_t drawbuf_capacity_bytes; /* Per-slot writable capacity in bytes. */
    size_t buffer_alignment;       /* Power-of-two base alignment in bytes. */
    uint32_t max_tile_rows;
    uint32_t max_damage_areas;
    bool retains_content;
    bool supports_present_confirmation;
    bool writes_active_scanout; /* Explicit persistent single-FB contract. */
} esp_display_present_backend_caps_t;

typedef struct {
    const esp_display_present_rect_t *damage;
    size_t damage_count;
    /* Producer redraws every pixel without relying on the old contents of
     * the acquired surface. Actual coverage is still checked before publish
     * if prior pixels were not seeded. */
    bool full_redraw;
} esp_display_present_frame_request_t;

typedef struct {
    esp_display_present_lease_id_t id;
    esp_display_present_surface_t surface;
    esp_display_present_rect_t logical_area; /* Surface origin maps to logical_area origin. */
} esp_display_present_lease_t;

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
    esp_display_present_rect_t logical_area;
    size_t stride_bytes;
} esp_display_present_region_submit_t;

/** Optional explicit dirty coverage when sealing a FULL/DIRECT frame.
 * Ignored by GRAM (which tracks each submitted partition region directly). */
typedef struct {
    const esp_display_present_rect_t *areas;
    size_t area_count;
    bool full_coverage;
} esp_display_present_frame_coverage_t;

typedef enum {
    ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED,
    ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED,
} esp_display_present_completion_point_t;

typedef struct {
    bool sealed;
    /** Conservative frame-source completion: accepted frame work has drained.
     * May lag individual tile reuse; does not grant permission to write a
     * retained scanout/baseline allocation. Acquire a new lease before writes.
     * Terminal failure may release sources without confirming presentation. */
    bool source_released;
    bool present_confirmed;
    bool terminal; /* No further progress for this frame receipt. */
    esp_err_t result;
} esp_display_present_frame_status_t;

/** Sticky first accepted-operation failure, retained until backend destruction,
 * including fire-and-forget frames. ESP_OK means no failure observed. */
typedef struct {
    esp_err_t result;
    esp_display_present_frame_t frame;
    uint64_t transaction_cookie;
} esp_display_present_backend_fault_t;

/** Adapter-normalized event. Platforms must define cookie correlation, event
 * ordering, and the precise buffer-retirement meaning. A raw VSYNC is not
 * automatically a scanout retirement event. */
typedef enum {
    ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,
    ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED,
    ESP_DISPLAY_PRESENT_EVENT_TE_EDGE,
    ESP_DISPLAY_PRESENT_EVENT_FAULT,
} esp_display_present_event_kind_t;

typedef struct {
    esp_display_present_event_kind_t kind;
    uint64_t transaction_cookie;
    uint64_t timestamp_us;
    esp_err_t result;
} esp_display_present_backend_event_t;

/** Driver adapter: rejected means no access and no callback; accepted/uncertain
 * consumes the source until completion or successful quiesce. */
typedef enum {
    ESP_DISPLAY_PRESENT_TX_REJECTED,
    ESP_DISPLAY_PRESENT_TX_ACCEPTED,
    ESP_DISPLAY_PRESENT_TX_UNCERTAIN,
} esp_display_present_tx_acceptance_t;

typedef struct {
    esp_display_present_tx_acceptance_t acceptance;
    esp_err_t result;
} esp_display_present_tx_result_t;

typedef bool (*esp_display_present_panel_event_fn)(void *user, const esp_display_present_backend_event_t *event, bool from_isr);

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
    esp_err_t (*attach)(void *context, esp_display_present_panel_event_fn callback, void *user);
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
    esp_display_present_tx_result_t (*submit)(void *context, const esp_display_present_surface_t *surface, const esp_display_present_rect_t *area, uint64_t cookie);
    /**
     * @brief Success guarantees no accesses to framework-owned pixels and no
     * active/future callbacks. An explicitly retained, immutable driver-owned
     * external FB may continue scanout until its panel owner deletes it; it
     * must never be freed by this backend. All other accesses must end.
     * Failure leaves registration/resources valid for retry. Must honor total
     * timeout budget; may safely abort hardware rather than finish display.
     *
     * @param context Borrowed implementation context.
     * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
     * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
     */
    esp_err_t (*quiesce)(void *context, uint32_t timeout_ms);
} esp_display_present_panel_adapter_t;

typedef bool (*esp_display_present_progress_fn)(void *user, bool from_isr);

/* Advanced override. The default presenter_create path does not use this: it
 * binds the internal GPIO sampler after create and then clears any placeholder
 * window. Borrowed until stop; task-side admit
 * runs outside backend locks and may wait for a bounded hardware window.
 * TIMEOUT means no admission (retry), never sticky fault. A successful return
 * supplies the actual selected edge; rejected transfers must wait for a newer
 * edge. done is ISR-safe and observes real completion, or definite rejection.
 * No source ownership, image pools or frame lifecycle belongs to this policy. */
typedef struct {
    esp_err_t (*admit)(void *, size_t transfer_bytes, uint64_t after_edge, uint64_t *edge);
    void (*start)(void *);
    void (*done)(void *, esp_err_t);
    void *user;
} esp_display_present_te_gate_t;

struct esp_display_present_backend_config {
    uint32_t width, height;     /* Logical dimensions seen by the producer. */
    uint32_t tile_rows;         /* GRAM/TE partition height in rows. */
    size_t buffer_count;        /* 1..16 draw/frame buffers. */
    size_t max_frames;          /* 1..32 bounded frame/receipt records. */
    esp_display_present_engine_pixel_format_t format;
    esp_display_present_byte_order_t draw_order;
    esp_display_present_byte_order_t panel_order;
    esp_display_present_engine_rotation_t rotation;    /* Logical-to-physical clockwise rotation. */
    bool in_place_convert;      /* True: swap byte order in-place on tight stride and revert on rejection. */
    void *const *external_buffers; /* Optional caller/driver-owned FB array (TE/FB). */
    void *initial_scanout_buffer; /* Optional member of external_buffers already scanning; not writable until retired. */
    size_t external_buffer_bytes;  /* Capacity of each external buffer. */
    esp_display_present_panel_adapter_t panel;  /* Borrowed until successful stop. */
    /** TE only: measured safe window from edge and worst-case whole-frame
     * transfer budget (including dispatch overhead). Both nonzero, budget < window.
     * GRAM ignores these fields. No hardware timing is inferred automatically. */
    uint32_t te_window_us;
    uint32_t te_transfer_budget_us;
    /** FB: 1..3 framebuffers in buffer_count, one scratch tile for PARTITION.
     * FULL/DIRECT leases expose the whole candidate; submit coordinates index
     * that full surface (not scratch origin). Zero defaults to PARTITION. */
    esp_display_present_render_contract_t render_contract;
    /** Single-FB is permitted only with explicit acceptance of active scanout
     * writes/tearing. Cancel cannot roll back pixels already visible. */
    bool allow_active_scanout_writes;
    /** GRAM/TE and FB PARTITION queued tile copies. Defaults retain synchronous CPU operation.
     * Executor must obey pixel_execute's return-means-access-ended contract.
     * FB FULL/DIRECT reject async_copy; those contracts draw directly.
     * GRAM async_copy requires separate conversion storage (no in_place_convert). */
    bool async_copy;
    esp_display_present_pixel_execute_fn copy_execute;
    void *copy_user;
    /** Optional wake only, outside locks; cannot reenter backend task APIs.
     * Caller context remains valid through successful stop. */
    esp_display_present_progress_fn on_progress;
    void *progress_user;
    bool tile_in_psram, image_in_psram, logical_in_psram;
    bool fixed_logical_rotation; /* Runtime rotation preserves producer dimensions; physical view may transpose. */
    bool internal_tx_staging; /* GRAM async copies from external tiles to internal DMA conversion slots. */
    size_t transfer_capacity; /* GRAM: 0 uses tile count. */
    esp_display_present_te_gate_t te_gate; /* Explicit injection; default retains native timing model. */
    esp_display_present_te_timing_t te_timing; /* period_us=0 retains the explicit fixed edge window. */
    size_t tile_buffer_count; /* FB PARTIAL: independent draw pool; 0 legacy one. */
    size_t compose_count; /* TE: 0 legacy serialized dual-image, explicit 1/2 enables pool scheduling. */
};

/** FB adapters borrow the current surface continuously until a successful
 * SCANOUT_CONFIRMED for its replacement proves BOTH new activation and old
 * retirement, or quiesce succeeds. TRANSFER_DONE/raw VSYNC is insufficient.
 * Completion receipt releases producer access, never the scanout allocation;
 * only a new lease grants write permission. External buffers must be exclusively
 * handed off, 64-byte aligned, disjoint, and live until successful stop. */

typedef struct present_backend present_backend_t;

typedef struct {
    const char *name;
    esp_err_t (*create)(const esp_display_present_backend_config_t *config, present_backend_t **out_backend);
    esp_err_t (*get_caps)(present_backend_t *backend, esp_display_present_backend_caps_t *caps);
    esp_err_t (*begin)(present_backend_t *backend, const esp_display_present_frame_request_t *request, esp_display_present_frame_t *frame);
    esp_err_t (*acquire)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_rect_t *area, uint32_t timeout_ms, esp_display_present_lease_t *lease);
    esp_err_t (*get_draw_buffers)(present_backend_t *backend, void **out_buffers, size_t max_buffers, size_t *out_count, size_t *out_buffer_bytes);
    esp_err_t (*submit_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id);
    esp_err_t (*submit_region_ex)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id, const esp_display_present_region_submit_t *submit);
    esp_err_t (*cancel_region)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease_id);
    esp_err_t (*end)(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_receipt_t *out_receipt);
    esp_err_t (*end_ex)(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_frame_coverage_t *coverage, esp_display_present_receipt_t *out_receipt);
    esp_err_t (*cancel_frame)(present_backend_t *backend, esp_display_present_frame_t frame, uint32_t timeout_ms);
    esp_err_t (*query)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_frame_status_t *status);
    esp_err_t (*get_fault)(present_backend_t *backend, esp_display_present_backend_fault_t *fault);
    esp_err_t (*wait)(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_completion_point_t point, uint32_t timeout_ms, esp_display_present_frame_status_t *status);
    esp_err_t (*release_receipt)(present_backend_t *backend, esp_display_present_receipt_t receipt);
    bool (*on_event_isr)(present_backend_t *backend, const esp_display_present_backend_event_t *event);
    void (*on_event)(present_backend_t *backend, const esp_display_present_backend_event_t *event);
    esp_err_t (*stop)(present_backend_t *backend, uint32_t timeout_ms);
    esp_err_t (*destroy)(present_backend_t *backend);
    esp_err_t (*set_rotation)(present_backend_t *backend, esp_display_present_engine_rotation_t rotation);
} present_backend_ops_t;

extern const present_backend_ops_t present_gram_backend;
extern const present_backend_ops_t present_te_backend;
extern const present_backend_ops_t present_fb_backend;

esp_err_t present_gram_service(present_backend_t *backend);
esp_err_t present_te_create_scheduled(const esp_display_present_backend_config_t *, present_backend_t **, bool autonomous);
esp_err_t present_te_service(present_backend_t *backend);
esp_err_t present_fb_service(present_backend_t *backend);

#ifdef __cplusplus
}
#endif
