/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_types.h"
#include "esp_display_present_renderer_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_display_present_backend_config esp_display_present_backend_config_t;
typedef struct {
    uint64_t value;
} esp_display_present_frame_t;
typedef struct {
    uint64_t value;
} esp_display_present_receipt_t;

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
    bool src_released;
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
     * @brief Descriptor valid during call. region/TE pixels until completion; FB pixels
     * through continuous scanout until confirmed replacement or quiesce.
     * Contract for region/IO panels: surface must be tightly packed
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
    /* Task-only, nonblocking and idempotent. Stops future bands, never aborts DMA. */
    void (*request_abort)(void *context);
} esp_display_present_panel_adapter_t;

typedef bool (*esp_display_present_progress_fn)(void *user, bool from_isr);

/** FB adapters borrow the current surface continuously until a successful
 * SCANOUT_CONFIRMED for its replacement proves BOTH new activation and old
 * retirement, or quiesce succeeds. TRANSFER_DONE/raw VSYNC is insufficient.
 * Completion receipt releases producer access, never the scanout allocation;
 * only a new lease grants write permission. External buffers must be exclusively
 * handed off, 64-byte aligned, disjoint, and live until successful stop. */

typedef struct present_backend present_backend_t;

#ifdef __cplusplus
}
#endif
