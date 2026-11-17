/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file
 * @brief Renderer-facing public entry point for the presenter draw contract and lifecycle API.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_display_present_config.h"
#include "esp_display_present_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default per-push panel transfer completion budget in milliseconds. */
#define ESP_DISPLAY_PRESENT_DEFAULT_TRANSFER_TIMEOUT_MS 1000U

/** @brief Default FB pipeline free-buffer acquire budget in milliseconds. */
#define ESP_DISPLAY_PRESENT_DEFAULT_PIPELINE_ACQUIRE_TIMEOUT_MS 1000U

/** @brief Frame receipt API version. */
#define ESP_DISPLAY_PRESENT_FRAME_COMPLETION_API_VERSION 1U

/**
 * @brief Query the minimum number of panel framebuffers before creating the panel.
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
 * @brief How GSP should talk to this presenter.
 *
 * This is the public draw contract, not the transport implementation.
 * Transport and framebuffer policy stay inside the presenter.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_CONTRACT_PARTITION = 0, /**< Submit partitioned areas through a shared draw buffer. */
    ESP_DISPLAY_PRESENT_CONTRACT_DIRECT,        /**< Submit dirty regions into a coherent draw surface. */
    ESP_DISPLAY_PRESENT_CONTRACT_FULL,          /**< Submit a full frame. */
} esp_display_present_contract_t;

/** @brief Coverage declaration for a frame at commit. */
typedef enum {
    ESP_DISPLAY_PRESENT_COVERAGE_FULL = 0, /**< The complete logical screen has been produced. */
    ESP_DISPLAY_PRESENT_COVERAGE_AREAS,    /**< Only listed logical areas have been produced. */
} esp_display_present_coverage_t;

/** @brief Pixel surface description. */
typedef struct {
    void *pixels; /**< Address of the first pixel. */
    size_t stride_bytes; /**< Distance between adjacent rows in bytes. */
    uint16_t width; /**< Surface width in pixels. */
    uint16_t height; /**< Surface height in pixels. */
    esp_display_present_pixel_format_t pixel_format; /**< Pixel storage format. */
} esp_display_presenter_surface_t;

/** @brief Presenter creation parameters. */
typedef struct {
    uint16_t width;  /**< Logical display width in pixels. */
    uint16_t height; /**< Logical display height in pixels. */
    esp_display_present_pixel_format_t pixel_format; /**< Renderer output format; must match target.hw.input_pixel_format. */
    size_t max_damage_areas; /**< Maximum dirty areas accepted per frame. */
    uint32_t transfer_timeout_ms; /**< Per-push panel transfer completion budget in ms (0 selects default); timeout latches a terminal fault. */
    uint32_t pipeline_acquire_timeout_ms; /**< FB pipeline free-buffer acquire budget in ms (0 selects default). */
    esp_display_present_target_config_t target; /**< Panel hardware, present mode and endpoint memory configuration. */
} esp_display_presenter_config_t;

/** @brief Initialized panel binding for the one-call presenter setup. */
typedef struct {
    uint16_t width;  /**< Physical panel width in pixels. */
    uint16_t height; /**< Physical panel height in pixels. */
    esp_display_present_hw_config_t hw; /**< Borrowed panel, pixel format and bus settings. */
} esp_display_presenter_panel_config_t;

/** @brief Renderer-visible capabilities of a presenter. */
typedef struct {
    esp_display_present_contract_t contract; /**< Public draw contract seen by GSP. */
    bool supports_coverage_areas;            /**< True for FB DIRECT and PARTIAL, which accept explicit area hints. */
    bool surface_retains_content;            /**< DIRECT/FULL only: pixels remain valid across frames for dirty rendering. */
    bool previous_surface_readable;          /**< DIRECT/FULL only: previous displayed surface can be read for scroll blits. */
    uint16_t width;                          /**< Logical display width in pixels. */
    uint16_t height;                         /**< Logical display height in pixels. */
    size_t stride_bytes;                     /**< Preferred full-surface row stride in bytes. */
    esp_display_present_pixel_format_t pixel_format; /**< Renderer output format. */
    size_t drawbuf_bytes;                    /**< PARTITION only: bytes available in one acquired draw region. */
    size_t max_damage_areas;                 /**< Maximum dirty areas accepted per frame. */
} esp_display_presenter_caps_t;

/** @brief Reasons for selecting a reduced configuration (bitmask flags). */
enum {
    ESP_DISPLAY_PRESENT_DEGRADE_AUTO_FB_POOL = 1u << 0, /**< AUTO resolved to two driver FBs. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_SINGLE_CORE = 1u << 1, /**< Auto overlap pool limited to one tile. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_HEADROOM = 1u << 2, /**< Internal DMA heap headroom limited the auto pool. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_ALLOCATION = 1u << 3, /**< Two-tile allocation failed and one tile succeeded. */
    ESP_DISPLAY_PRESENT_DEGRADE_TILE_PSRAM = 1u << 4, /**< Requested PSRAM tiles use internal DMA memory. */
    ESP_DISPLAY_PRESENT_DEGRADE_COMPOSE_PSRAM = 1u << 5, /**< TE compose images use internal DMA memory. */
    ESP_DISPLAY_PRESENT_DEGRADE_CANVAS_PSRAM = 1u << 6, /**< FB conversion canvas uses internal DMA memory. */
};

/** @brief Snapshot of the successfully applied presenter configuration. */
typedef struct {
    esp_display_present_mode_t requested_mode; /**< Mode requested at creation. */
    esp_display_present_mode_t effective_mode; /**< Mode actually resolved and active. */
    esp_display_present_rotation_t rotation;   /**< Active logical-to-physical rotation. */
    uint16_t width, height;                    /**< Logical dimensions. */
    size_t draw_stride_bytes;                  /**< Full-width producer stride; a tile lease can be narrower. */
    size_t output_stride_bytes;                /**< Packed full-width physical row, before panel transport. */
    uint8_t draw_buffer_count;                 /**< Writable pool size, including full-frame leases. */
    uint8_t tile_count;                        /**< Number of PARTITION input tiles; zero for FULL/DIRECT. */
    uint8_t image_count;                       /**< Driver FBs or TE compose images; zero for REGION. */
    esp_display_present_memory_domain_t draw_memory;  /**< Memory domain of draw buffers. */
    esp_display_present_memory_domain_t image_memory; /**< Memory domain of driver FBs or TE compose images. */
    uint32_t degradation_flags;                /**< ESP_DISPLAY_PRESENT_DEGRADE_* bitmask. */
} esp_display_presenter_effective_config_t;

/** @brief Coverage supplied when submitting a DIRECT or FULL surface. */
typedef struct {
    esp_display_present_coverage_t coverage; /**< Explicit coverage mode; FULL ignores areas, AREAS requires non-empty areas. */
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

/** @brief Producer-owned drawable-buffer lease. */
typedef struct {
    esp_display_presenter_surface_t surface; /**< Preferred drawable surface layout. */
    size_t capacity_bytes; /**< Authoritative writable storage capacity. */
    uint32_t lease_id; /**< Identifier required by resolve, submit, or cancel. */
    esp_display_presenter_resolve_rows_cb_t resolve_rows; /**< Resolve a legal row count after the producer chooses its stride. */
    void *resolve_rows_ctx; /**< Opaque context for @c resolve_rows. */
} esp_display_presenter_buffer_t;

/** @brief Opaque presenter instance handle. */
typedef struct esp_display_presenter esp_display_presenter_t;

/** @brief Monotonic presenter lifecycle state. */
typedef enum {
    ESP_DISPLAY_PRESENTER_STATE_OPEN = 0, /**< Active and accepting frame production. */
    ESP_DISPLAY_PRESENTER_STATE_CLOSING,  /**< Draining pending work before stop. */
    ESP_DISPLAY_PRESENTER_STATE_STOPPED,  /**< Quiescent and ready for deletion. */
    ESP_DISPLAY_PRESENTER_STATE_FAULTED,  /**< Latched terminal fault; delete and recreate to recover. */
} esp_display_presenter_state_t;

/**
 * @brief Open a presenter on an initialized panel using the requested mode and default resource policy.
 *
 * 90/270 degree rotation exchanges the logical axes; keep width and height in
 * the panel's physical orientation. AUTO resolves against the supplied driver
 * framebuffer pool or TE config. The presenter borrows the panel and IO until
 * deletion, adopts the calling task as producer, and prepares the first scanout
 * or TE window on first frame admission. @p out_presenter is set to NULL on error.
 */
esp_err_t esp_display_presenter_open(
    const esp_display_presenter_panel_config_t *panel,
    esp_display_present_mode_t mode,
    esp_display_presenter_t **out_presenter);

/**
 * @brief Open a presenter with a custom tile policy and optional TE compose count.
 *
 * A NULL policy uses the defaults of esp_display_presenter_open().
 * See esp_display_presenter_create() for timeout and damage overrides.
 */
esp_err_t esp_display_presenter_open_ex(
    const esp_display_presenter_panel_config_t *panel,
    esp_display_present_mode_t mode,
    const esp_display_present_drawbuf_config_t *drawbuf,
    esp_display_presenter_t **out_presenter);

/**
 * @brief Create a renderer endpoint and bind all required target callbacks.
 *
 * Renderer calls are single-task and serialize acquire, submit and cancel.
 * A control task may request stop, but delete has one exclusive owner and
 * must not race another API call. Hardware callbacks run in ISR context;
 * the component keeps their state alive until stop has drained them.
 */
esp_err_t esp_display_presenter_create(
    const esp_display_presenter_config_t *config,
    esp_display_presenter_t **out_presenter);

/**
 * @brief Enter CLOSING and stop after all asynchronous work has retired.
 *
 * New acquire calls fail with ESP_ERR_INVALID_STATE. An active lease must be
 * cancelled by its renderer. Retry ESP_ERR_INVALID_STATE while renderer calls
 * or hardware work are in flight. After a terminal fault, resume rendering
 * only after safe stop, deletion and recreation.
 */
esp_err_t esp_display_presenter_stop(
    esp_display_presenter_t *presenter);

/**
 * @brief Stop, detach callbacks and release the target and endpoint.
 *
 * On ESP_ERR_INVALID_STATE the object remains owned by the caller and must be
 * retained for a later retry. Do not call delete concurrently from two tasks.
 */
esp_err_t esp_display_presenter_delete(
    esp_display_presenter_t *presenter);

/**
 * @brief Reconfigure logical-to-physical rotation without changing panel scan mode.
 *
 * The producer must first stop submitting and quiesce the presenter. Crossing
 * between 0/180 and 90/270 swaps the logical width and height; query get_caps()
 * again before rendering. On failure, the previous configuration remains valid.
 */
esp_err_t esp_display_presenter_set_rotation(
    esp_display_presenter_t *presenter,
    esp_display_present_rotation_t rotation);

/** @brief Query renderer-visible presenter capabilities. */
esp_err_t esp_display_presenter_get_caps(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_caps_t *out_caps);

/**
 * @brief Query the effective configuration from task context (serialize with deletion).
 *
 * Describes the current layout, including after a successful rotation. Pool
 * counts describe allocated slots, not currently available slots. Allocates
 * no storage and starts no transfers.
 */
esp_err_t esp_display_presenter_get_effective_config(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_effective_config_t *out_config);

/**
 * @brief Enable and reset opt-in performance sampling.
 *
 * The first call allocates sampler storage. Call between producer runs with
 * no pending commits.
 */
esp_err_t esp_display_presenter_reset_metrics(esp_display_presenter_t *presenter);

/**
 * @brief Read the current sampling window without driving the backend.
 *
 * Returns ESP_ERR_INVALID_STATE if sampling has never been enabled. Serialize
 * both metrics APIs with deletion.
 */
esp_err_t esp_display_presenter_get_metrics(const esp_display_presenter_t *presenter,
                                            esp_display_presenter_metrics_t *out);

/**
 * @brief PARTITION only: inspect the presenter's draw-buffer pool.
 *
 * Acquire a buffer before drawing and write only through the returned lease's
 * surface while that lease is valid. Query again after changing rotation and
 * discard the addresses on deletion.
 */
typedef struct {
    void *buffers[2]; /**< Borrowed draw-buffer addresses. */
    uint8_t count; /**< Number of valid draw-buffer addresses. */
    uint16_t lines; /**< Preferred draw-buffer height in rows. */
    size_t bytes; /**< Capacity of each draw buffer in bytes. */
} esp_display_presenter_drawbuf_t;

/** @brief Inspect the presenter draw-buffer pool in PARTITION modes. */
esp_err_t esp_display_presenter_get_drawbuf(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_drawbuf_t *out_drawbuf);

/**
 * @brief Read the minimum power-of-two base-address alignment (bytes) of all leasable buffers.
 *
 * Query again after changing rotation, which rebuilds renderer storage.
 */
esp_err_t esp_display_presenter_get_buffer_alignment(
    const esp_display_presenter_t *presenter, size_t *out_alignment);

/**
 * @brief Rebind the serial render-task owner after a platform session handoff.
 *
 * Valid only between frames; retains pending hardware completion fences.
 */
esp_err_t esp_display_presenter_rebind_producer(
    esp_display_presenter_t *presenter);

/**
 * @brief Release the calling task's producer ownership between frames.
 *
 * Use when the next renderer starts on a task that cannot call rebind_producer
 * itself; its first admitted begin_next_frame claims ownership.
 */
esp_err_t esp_display_presenter_release_producer(
    esp_display_presenter_t *presenter);

/** @brief Read the monotonic lifecycle state; FAULTED never reopens in place. */
esp_err_t esp_display_presenter_get_state(
    const esp_display_presenter_t *presenter,
    esp_display_presenter_state_t *out_state);

/**
 * @brief Read why the presenter faulted (ESP_DISPLAY_PRESENT_FAULT_NONE if none).
 */
esp_err_t esp_display_presenter_get_fault_reason(
    const esp_display_presenter_t *presenter,
    esp_display_present_fault_reason_t *out_reason);

/**
 * @brief Read the original error behind the latched fault (ESP_OK if none).
 */
esp_err_t esp_display_presenter_get_fault_error(
    const esp_display_presenter_t *presenter,
    esp_err_t *out_error);

/** @brief Map physical input coordinates into the logical render orientation. */
esp_err_t esp_display_presenter_map_point(
    const esp_display_presenter_t *presenter,
    int32_t *x,
    int32_t *y);

/**
 * @brief Begin a frame on the presenter timeline and return logical areas needing rasterization.
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

/**
 * @brief Commit the active frame and return a completion receipt (cleared on failure).
 *
 * When @p track_display is true, hardware VSYNC is required and later frames
 * wait for the switch fence plus two VSYNCs or cancel_frame_tracking().
 */
esp_err_t esp_display_presenter_commit_frame_with_receipt(
    esp_display_presenter_t *presenter,
    const esp_display_presenter_submit_t *submit,
    bool track_display,
    esp_display_presenter_frame_receipt_t *out_receipt);

/**
 * @brief Read a coherent completion snapshot from task context without waiting.
 */
esp_err_t esp_display_presenter_get_frame_status(
    esp_display_presenter_t *presenter,
    esp_display_presenter_frame_status_t *out_status);

/**
 * @brief Cancel the confirmation wait matching @p frame_id and @p generation.
 *
 * Returns ESP_ERR_NOT_FOUND if no matching wait remains. Does not confirm the
 * frame or release hardware-owned buffers.
 */
esp_err_t esp_display_presenter_cancel_frame_tracking(
    esp_display_presenter_t *presenter, uint64_t frame_id, uint32_t generation);

/** @brief Cancel the active pre-commit frame and any held region from its bound producer task. */
void esp_display_presenter_cancel_frame(
    esp_display_presenter_t *presenter);

/**
 * @brief Drain work accepted before this call while the producer stays paused between frames.
 *
 * REGION and TE wait for source reads to finish; FB waits for scanout switch
 * confirmation (plus confirmation VSYNCs for tracked receipts). Returns
 * ESP_ERR_TIMEOUT on expiry or ESP_ERR_INVALID_STATE if faulted.
 */
esp_err_t esp_display_presenter_quiesce(
    esp_display_presenter_t *presenter,
    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
