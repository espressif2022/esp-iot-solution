/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* Renderer-facing public entry point: the presenter draw contract and
 * lifecycle API. Low-level surface/partition APIs require explicit includes
 * and are implementation details for most integrations. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_display_present_config.h"
#include "esp_display_present_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default per-push panel transfer completion budget. */
#define ESP_DISPLAY_PRESENT_DEFAULT_TRANSFER_TIMEOUT_MS 1000U

/** Default FB pipeline free-buffer acquire budget. */
#define ESP_DISPLAY_PRESENT_DEFAULT_PIPELINE_ACQUIRE_TIMEOUT_MS 1000U

/** Frame receipt API version. */
#define ESP_DISPLAY_PRESENT_FRAME_COMPLETION_API_VERSION 1U

/**
 * Query the minimum number of panel framebuffers before creating the panel.
 *
 * Set an explicit RGB, MIPI-DSI or IO panel type. Only panel type, mode,
 * rotation, TE configuration and an optional AUTO frame_buffer_count hint
 * are read; handles, framebuffer addresses, pixel format and draw-buffer
 * settings are ignored. With no count hint, scanout AUTO recommends three
 * framebuffers; creation resolves against the actual driver pool. This query performs
 * static policy validation without accessing hardware or allocating memory.
 *
 * IO/GRAM returns 0: draw buffers and TE compose buffers are separate.
 * Presenter creation still validates the actual handles and buffer storage.
 * @p out_count is set to 0 on error when non-NULL.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG for invalid arguments,
 *         ESP_ERR_NOT_SUPPORTED for an unresolved panel type or unsupported
 *         mode, rotation or TE combination.
 */
esp_err_t esp_display_present_get_required_frame_buffer_count(
    const esp_display_present_target_config_t *config,
    uint8_t *out_count);

/**
 * How GSP should talk to this presenter.
 *
 * This is the public draw contract, not the transport implementation.
 * Transport and framebuffer policy stay inside the presenter.
 */
typedef enum {
    /** Submit partitioned areas through a shared draw buffer. */
    ESP_DISPLAY_PRESENT_CONTRACT_PARTITION = 0,
    /** Submit dirty regions into a coherent draw surface. */
    ESP_DISPLAY_PRESENT_CONTRACT_DIRECT,
    /** Submit a full frame. */
    ESP_DISPLAY_PRESENT_CONTRACT_FULL,
} esp_display_present_contract_t;

/** Coverage declaration for a frame at commit. */
typedef enum {
    /** The complete logical screen has been produced. */
    ESP_DISPLAY_PRESENT_COVERAGE_FULL = 0,
    /** Only listed logical areas have been produced. */
    ESP_DISPLAY_PRESENT_COVERAGE_AREAS,
} esp_display_present_coverage_t;

/** Pixel surface description. */
typedef struct {
    void *pixels; /**< Address of the first pixel. */
    size_t stride_bytes; /**< Distance between adjacent rows in bytes. */
    uint16_t width; /**< Surface width in pixels. */
    uint16_t height; /**< Surface height in pixels. */
    esp_display_present_pixel_format_t pixel_format; /**< Pixel storage format. */
} esp_display_presenter_surface_t;

/** Presenter creation parameters. */
typedef struct {
    uint16_t width;  /**< Logical display width in pixels. */
    uint16_t height; /**< Logical display height in pixels. */
    /** Renderer output format; must match target.hw.input_pixel_format. */
    esp_display_present_pixel_format_t pixel_format;
    size_t max_damage_areas; /**< Maximum dirty areas accepted per frame. */
    /**
     * Per-push panel transfer completion budget in milliseconds; 0 selects
     * ESP_DISPLAY_PRESENT_DEFAULT_TRANSFER_TIMEOUT_MS. A timeout faults the
     * presenter with ESP_DISPLAY_PRESENT_FAULT_TRANSFER_TIMEOUT, so raise
     * this for slow panels instead of accepting a terminal fault.
     */
    uint32_t transfer_timeout_ms;
    /**
     * FB pipeline free-buffer acquire budget in milliseconds; 0 selects
     * ESP_DISPLAY_PRESENT_DEFAULT_PIPELINE_ACQUIRE_TIMEOUT_MS.
     */
    uint32_t pipeline_acquire_timeout_ms;
    /** Panel hardware, present mode and endpoint memory configuration. */
    esp_display_present_target_config_t target;
} esp_display_presenter_config_t;

/** Initialized panel binding for the one-call presenter setup. */
typedef struct {
    uint16_t width;  /**< Physical panel width in pixels. */
    uint16_t height; /**< Physical panel height in pixels. */
    esp_display_present_hw_config_t hw; /**< Borrowed panel, pixel format and bus settings. */
} esp_display_presenter_panel_config_t;

/** Renderer-visible capabilities of a presenter. */
typedef struct {
    /** Public draw contract seen by GSP. */
    esp_display_present_contract_t contract;
    /** True for FB DIRECT and PARTIAL, which accept explicit area hints. */
    bool supports_coverage_areas;
    /** DIRECT/FULL only: pixels remain valid across frames for dirty rendering. */
    bool surface_retains_content;
    /** DIRECT/FULL only: previous displayed surface can be read for scroll blits. */
    bool previous_surface_readable;
    uint16_t width;  /**< Logical display width in pixels. */
    uint16_t height; /**< Logical display height in pixels. */
    size_t stride_bytes; /**< Preferred full-surface row stride in bytes. */
    esp_display_present_pixel_format_t pixel_format; /**< Renderer output format. */
    /** PARTITION only: bytes available in one acquired draw region. */
    size_t drawbuf_bytes;
    size_t max_damage_areas; /**< Maximum dirty areas accepted per frame. */
} esp_display_presenter_caps_t;

/** Reasons for selecting a reduced configuration. Flags can be combined. */
enum {
    ESP_DISPLAY_PRESENT_DEGRADE_AUTO_FB_POOL = 1u << 0, /**< AUTO resolved to two driver FBs. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_SINGLE_CORE = 1u << 1, /**< Auto overlap pool limited to one tile. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_HEADROOM = 1u << 2, /**< Internal DMA heap headroom limited the auto pool. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_ALLOCATION = 1u << 3, /**< Two-tile allocation failed and one tile succeeded. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_PSRAM = 1u << 4, /**< Requested PSRAM tiles use internal DMA memory. */
    ESP_DISPLAY_PRESENT_DEGRADE_COMPOSE_PSRAM = 1u << 5, /**< TE compose images use internal DMA memory. */
    ESP_DISPLAY_PRESENT_DEGRADE_CANVAS_PSRAM = 1u << 6, /**< FB conversion canvas uses internal DMA memory. */
};

/** Snapshot of the successfully applied presenter configuration. */
typedef struct {
    esp_display_present_mode_t requested_mode;
    esp_display_present_mode_t effective_mode;
    esp_display_present_rotation_t rotation;
    uint16_t width, height;             /**< Logical dimensions. */
    size_t draw_stride_bytes;           /**< Full-width producer stride; a tile lease can be narrower. */
    size_t output_stride_bytes;         /**< Packed full-width physical row, before panel transport. */
    uint8_t draw_buffer_count;          /**< Writable pool size, including full-frame leases. */
    uint8_t tile_count;                 /**< Number of PARTITION input tiles; zero for FULL/DIRECT. */
    uint8_t image_count;                /**< Driver FBs or TE compose images; zero for REGION. */
    esp_display_present_memory_domain_t draw_memory;
    esp_display_present_memory_domain_t image_memory;
    uint32_t degradation_flags;         /**< ESP_DISPLAY_PRESENT_DEGRADE_* bitmask. */
} esp_display_presenter_effective_config_t;

/** Coverage supplied when submitting a DIRECT or FULL surface. */
typedef struct {
    /**
     * Explicit coverage for a submitted surface. No-op updates are handled
     * before acquire/submit. FULL ignores areas. AREAS requires non-empty areas.
     */
    esp_display_present_coverage_t coverage;
    const esp_display_present_area_t *areas; /**< Covered logical areas for AREAS coverage. */
    size_t area_count; /**< Number of entries in @c areas. */
} esp_display_presenter_submit_t;

/**
 * @brief Producer-neutral drawable buffer lease.
 *
 * PARTITION/TE leases expose reusable scratch storage; FULL leases expose the
 * complete logical surface. The producer chooses the logical area and the
 * packed stride when submitting. @p capacity_bytes is the authoritative
 * storage limit; surface dimensions describe the presenter's preferred
 * full-width layout.
 */
typedef esp_err_t (*esp_display_presenter_resolve_rows_cb_t)(
    void *user_ctx, uint32_t lease_id, size_t stride_bytes,
    size_t requested_rows, size_t *out_rows);

/** Producer-owned drawable-buffer lease. */
typedef struct {
    esp_display_presenter_surface_t surface; /**< Preferred drawable surface layout. */
    size_t capacity_bytes; /**< Authoritative writable storage capacity. */
    uint32_t lease_id; /**< Identifier required by resolve, submit, or cancel. */
    /** Resolve a legal row count after the producer chooses its stride. */
    esp_display_presenter_resolve_rows_cb_t resolve_rows;
    void *resolve_rows_ctx; /**< Opaque context for @c resolve_rows. */
} esp_display_presenter_buffer_t;

typedef struct esp_display_presenter esp_display_presenter_t;

typedef enum {
    ESP_DISPLAY_PRESENTER_STATE_OPEN = 0,
    ESP_DISPLAY_PRESENTER_STATE_CLOSING,
    ESP_DISPLAY_PRESENTER_STATE_STOPPED,
    ESP_DISPLAY_PRESENTER_STATE_FAULTED,
} esp_display_presenter_state_t;

/**
 * Open a presenter on an initialized panel using the requested mode and
 * default resource policy. 90/270 degree rotation exchanges the logical
 * axes; keep width and height in the panel's physical orientation.
 *
 * AUTO resolves against the supplied driver framebuffer pool or TE config.
 * The presenter borrows the panel and IO until deletion. It adopts the
 * calling task as producer and prepares the first scanout or TE window on
 * first frame admission. @p out_presenter is set to NULL on error.
 */
esp_err_t esp_display_presenter_open(
    const esp_display_presenter_panel_config_t *panel,
    esp_display_present_mode_t mode,
    esp_display_presenter_t **out_presenter);

/**
 * Open with a custom tile policy and, for TE_SYNC, one or two compose buffers.
 * A NULL policy uses the defaults of esp_display_presenter_open().
 * See esp_display_presenter_create() for timeout and damage overrides.
 */
esp_err_t esp_display_presenter_open_ex(
    const esp_display_presenter_panel_config_t *panel,
    esp_display_present_mode_t mode,
    const esp_display_present_drawbuf_config_t *drawbuf,
    esp_display_presenter_t **out_presenter);

/**
 * Create a renderer endpoint and bind all required target callbacks.
 *
 * Renderer calls are single-task and serialize acquire, submit and
 * cancel. A control task may request stop, but delete has one exclusive owner
 * and must not race another API call. Hardware callbacks run in ISR context;
 * the component keeps their state alive until stop has drained them.
 */
esp_err_t esp_display_presenter_create(
    const esp_display_presenter_config_t *config,
    esp_display_presenter_t **out_presenter);

/**
 * Enter CLOSING and stop after all asynchronous work has retired. New acquire
 * calls fail with ESP_ERR_INVALID_STATE. An active lease must be cancelled by
 * its renderer. The acquisition budget also bounds waiting for the facade
 * gate and draining accepted work. Retry ESP_ERR_INVALID_STATE while renderer
 * calls or hardware work are in flight. After a terminal fault, resume
 * rendering only after safe stop, deletion and recreation.
 */
esp_err_t esp_display_presenter_stop(
    esp_display_presenter_t *presenter);

/**
 * Stop, detach callbacks and release the target and endpoint.
 *
 * On ESP_ERR_INVALID_STATE the object remains owned by the caller and must be
 * retained for a later retry. Do not call delete concurrently from two tasks.
 */
esp_err_t esp_display_presenter_delete(
    esp_display_presenter_t *presenter);

/**
 * Reconfigure logical-to-physical rotation without changing panel scan mode.
 *
 * The producer must first stop submitting and quiesce the presenter. The
 * presenter object and physical panel dimensions remain unchanged. Crossing
 * between 0/180 and 90/270 swaps the logical width and height; query get_caps()
 * again before rendering. Rotation-dependent producer storage and transforms
 * are updated after pending work drains. On failure, the previous configuration
 * remains valid.
 */
esp_err_t esp_display_presenter_set_rotation(
    esp_display_presenter_t *presenter,
    esp_display_present_rotation_t rotation);

esp_err_t esp_display_presenter_get_caps(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_caps_t *out_caps);

/**
 * Query the effective configuration from task context. Serialize with deletion.
 * The result describes the current layout, including after a successful rotation.
 * Pool counts describe allocated slots, not the number currently available.
 * Memory domains describe the producer and image pools; a borrowed framebuffer
 * can be shared by both. The snapshot grants no buffer lease.
 * The query allocates no storage and does not start transfers.
 */
esp_err_t esp_display_presenter_get_effective_config(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_effective_config_t *out_config);

/**
 * Enable and reset opt-in performance sampling. The first call allocates
 * sampler storage. Call between producer runs with no pending commits.
 * A frame already active at reset is excluded from frame latency samples.
 */
esp_err_t esp_display_presenter_reset_metrics(esp_display_presenter_t *presenter);

/**
 * Read the current sampling window without driving the backend. If sampling
 * has never been enabled, returns ESP_ERR_INVALID_STATE. Serialize both
 * metrics APIs with deletion. Scanout supply misses measure frame switches
 * under producer backlog; physical LCD DMA underruns require driver counters.
 */
esp_err_t esp_display_presenter_get_metrics(const esp_display_presenter_t *presenter,
                                            esp_display_presenter_metrics_t *out);

/**
 * PARTITION only: inspect the presenter's draw-buffer pool.
 *
 * These addresses are informational. Acquire a buffer before drawing and
 * write only through the returned lease's surface while that lease is valid.
 * A successful submit or frame cancellation ends the lease. Do not attach
 * an unleased pool address to a renderer as writable storage. Query the pool
 * again after changing rotation and discard the addresses on deletion.
 */
typedef struct {
    void *buffers[2]; /**< Borrowed draw-buffer addresses. */
    uint8_t count; /**< Number of valid draw-buffer addresses. */
    uint16_t lines; /**< Preferred draw-buffer height in rows. */
    size_t bytes; /**< Capacity of each draw buffer in bytes. */
} esp_display_presenter_drawbuf_t;

esp_err_t esp_display_presenter_get_drawbuf(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_drawbuf_t *out_drawbuf);

/**
 * Read the minimum base-address alignment of all buffers that this presenter
 * can lease to its renderer. The result is a power of two, in bytes. Query
 * again after changing rotation, which rebuilds the renderer storage.
 */
esp_err_t esp_display_presenter_get_buffer_alignment(
    const esp_display_presenter_t *presenter, size_t *out_alignment);

/**
 * Rebind the serial render-task owner after a platform session handoff.
 * Valid only between frames; it retains pending hardware completion fences.
 */
esp_err_t esp_display_presenter_rebind_producer(
    esp_display_presenter_t *presenter);

/**
 * Release the calling task's producer ownership between frames. Use when the
 * next renderer starts on a task that cannot call rebind_producer itself.
 * Stop the previous renderer and serialize startup of the next one. Its first
 * admitted begin_next_frame claims ownership. Explicit rebind or LVGL start
 * can also claim it. Existing hardware completion fences stay in effect.
 */
esp_err_t esp_display_presenter_release_producer(
    esp_display_presenter_t *presenter);

/** Read the monotonic lifecycle state; FAULTED never reopens in place. */
esp_err_t esp_display_presenter_get_state(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_state_t *out_state);

/**
 * Read why the presenter faulted. ESP_DISPLAY_PRESENT_FAULT_NONE means no
 * fault was recorded. A transfer timeout is recoverable by deleting and
 * recreating the presenter only when the endpoint can prove that hardware no
 * longer owns its buffers (for example the framebuffer endpoint's fault-stop
 * path). GRAM/TE endpoints must retain storage until their transfer callback
 * arrives. A producer protocol fault is a producer bug.
 */
esp_err_t esp_display_presenter_get_fault_reason(
    const esp_display_presenter_t *presenter,
    esp_display_present_fault_reason_t *out_reason);

/**
 * Read the original error behind the latched fault, before classification.
 * ESP_OK means no fault has been recorded. The value is the first accepted
 * operation or observation error that latched the fault; it is not reclassified
 * and never cleared in place.
 */
esp_err_t esp_display_presenter_get_fault_error(
    const esp_display_presenter_t *presenter,
    esp_err_t *out_error);

/** Map physical input coordinates into the logical render orientation. */
esp_err_t esp_display_presenter_map_point(
    const esp_display_presenter_t *presenter,
    int32_t *x,
    int32_t *y);

/**
 * Begin a frame on the presenter-owned timeline and return the logical areas
 * requiring rasterization. The asynchronous completion ticket is private to
 * the presenter.
 */
esp_err_t esp_display_presenter_begin_next_frame(
    esp_display_presenter_t *presenter,
    const esp_display_present_surface_request_t *request,
    esp_display_present_area_t *out_render_areas,
    size_t render_area_capacity,
    size_t *out_render_area_count,
    bool *out_full_coverage);

/** @brief Lease producer storage without choosing a logical damage area. */
esp_err_t esp_display_presenter_acquire_buffer(
    esp_display_presenter_t *presenter,
    esp_display_presenter_buffer_t *out_buffer);

/**
 * @brief Submit the rendered portion of a buffer lease.
 *
 * Pixels start at buffer.surface.pixels and use @p stride_bytes. The inclusive
 * logical @p area supplies the destination position and rendered dimensions.
 */
esp_err_t esp_display_presenter_submit_buffer(
    esp_display_presenter_t *presenter,
    const esp_display_presenter_buffer_t *buffer,
    const esp_display_present_area_t *area,
    size_t stride_bytes);

/** @brief Commit the active frame with explicit logical coverage. */
esp_err_t esp_display_presenter_commit_frame(
    esp_display_presenter_t *presenter,
    const esp_display_presenter_submit_t *submit);

/** Commit the active frame and return a receipt; clear it on failure.
 * Use instead of commit_frame(). Success identifies the frame; query status
 * for completion, checking that the status and receipt generations match.
 * track_display=false keeps the ordinary pipeline. true requires hardware
 * VSYNC and holds later frames until the switch fence plus two VSYNCs, or
 * cancel_frame_tracking(). A tracking-setup failure leaves the frame active:
 * retry without tracking or call cancel_frame() before starting another. */
esp_err_t esp_display_presenter_commit_frame_with_receipt(
    esp_display_presenter_t *presenter,
    const esp_display_presenter_submit_t *submit,
    bool track_display,
    esp_display_presenter_frame_receipt_t *out_receipt);

/** Read a coherent completion snapshot from task context without waiting.
 * Serialize with deletion. The endpoint determines the completion evidence. */
esp_err_t esp_display_presenter_get_frame_status(
    esp_display_presenter_t *presenter,
    esp_display_presenter_frame_status_t *out_status);

/** Cancel the confirmation wait matching frame_id and generation.
 * Returns ESP_ERR_NOT_FOUND if no matching wait remains. This neither confirms
 * the frame nor releases hardware-owned buffers. Task context; serialize with
 * deletion as for get_frame_status(). */
esp_err_t esp_display_presenter_cancel_frame_tracking(
    esp_display_presenter_t *presenter, uint64_t frame_id, uint32_t generation);

/** @brief Cancel the active pre-commit frame and any held region from its
 *  bound producer task. */
void esp_display_presenter_cancel_frame(
    esp_display_presenter_t *presenter);

/**
 * Drain work accepted before this call. The producer must stay paused
 * between frames for the entire call, and deletion must be serialized.
 *
 * REGION and TE wait for source reads to finish. FB waits for scanout switch
 * confirmation. An outstanding display-tracked receipt also waits for its
 * confirmation VSYNCs unless the caller cancels that tracking. The current
 * scanout surface remains owned by the panel.
 *
 * The nonzero timeout_ms covers gate acquisition, cancellation drain, and
 * completion waits together. UINT32_MAX waits indefinitely. ESP_ERR_TIMEOUT
 * leaves work and tracking available for a later retry. A faulted presenter
 * returns ESP_ERR_INVALID_STATE. Query get_fault_error() for the cause.
 */
esp_err_t esp_display_presenter_quiesce(
    esp_display_presenter_t *presenter,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
