/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_draw_types.h"
#include "esp_err.h"
#include "esp_display_present_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Instance-scoped draw lease identifier (0 is invalid). */
typedef struct {
    uint64_t value; /**< Lease token value. */
} esp_display_present_lease_id_t;

/** @brief Backend draw surface contract. */
typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION, /**< Submit partitioned regions through draw tiles. */
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL,      /**< Submit a complete logical surface per frame. */
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT,    /**< Submit dirty areas directly into retained full buffers. */
} esp_display_present_render_contract_t;

/** @brief Capabilities exposed by a resolved backend. */
typedef struct {
    esp_display_present_render_contract_t contract; /**< Active render contract. */
    esp_display_present_layout_t draw_layout;       /**< Preferred logical draw surface layout. */
    size_t drawbuf_capacity_bytes;                  /**< Per-slot writable capacity in bytes. */
    size_t buffer_alignment;                        /**< Power-of-two base alignment in bytes. */
    uint8_t draw_buffer_count;                      /**< Number of writable draw buffers. */
    uint8_t tile_count;                             /**< Number of PARTITION input tiles. */
    uint8_t image_count;                            /**< Number of driver FBs or TE compose images. */
    esp_display_present_memory_domain_t draw_memory;  /**< Memory domain of draw buffers. */
    esp_display_present_memory_domain_t image_memory; /**< Memory domain of driver FBs or TE compose images. */
    uint32_t max_tile_rows;                         /**< Maximum rows per partition tile. */
    uint32_t max_damage_areas;                      /**< Maximum damage rectangles per frame. */
    bool retains_content;                           /**< True when surface retains pixels across frames. */
    bool supports_present_confirmation;             /**< True when VSYNC display confirmation is supported. */
    bool writes_active_scanout;                     /**< Explicit persistent single-FB contract. */
} esp_display_present_backend_caps_t;

/** @brief Frame admission hints supplied to begin_next_frame(). */
typedef struct {
    const esp_display_present_rect_t *damage; /**< Optional logical damage hints. */
    size_t damage_count;                      /**< Number of entries in @c damage. */
    bool full_redraw;                         /**< Producer redraws every pixel without reading old surface contents. */
    bool resolved_pixels;                     /**< DIRECT only: submitted regions hold resolved pixels; skip readable prefill. */
} esp_display_present_frame_request_t;

/** @brief Acquired writable surface lease. */
typedef struct {
    esp_display_present_lease_id_t id;     /**< Lease token required by submit/cancel. */
    esp_display_present_surface_t surface; /**< Writable surface descriptor. */
    esp_display_present_rect_t logical_area; /**< Logical screen origin and extent for this lease. */
} esp_display_present_lease_t;

/**
 * @brief Optional explicit dirty coverage when sealing a FULL/DIRECT frame.
 *
 * Ignored by REGION, which tracks each submitted partition region directly.
 */
typedef struct {
    const esp_display_present_rect_t *areas; /**< Covered logical rectangles. */
    size_t area_count;                       /**< Number of entries in @c areas. */
    bool full_coverage;                      /**< True when the entire logical frame is covered. */
} esp_display_present_frame_coverage_t;

/** @brief Opaque internal presentation engine handle. */
typedef struct esp_display_present_engine esp_display_present_engine_t;

/** @brief Inclusive integer area (x1..x2, y1..y2). */
typedef struct {
    int32_t x1; /**< Inclusive left coordinate. */
    int32_t y1; /**< Inclusive top coordinate. */
    int32_t x2; /**< Inclusive right coordinate. */
    int32_t y2; /**< Inclusive bottom coordinate. */
} esp_display_present_engine_area_t;

/** @brief Internal engine lifecycle state. */
typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_RUNNING, /**< Accepting frames and driving backend work. */
    ESP_DISPLAY_PRESENT_ENGINE_CLOSING, /**< Draining pending work before stop. */
    ESP_DISPLAY_PRESENT_ENGINE_STOPPED, /**< Stopped and ready to destroy. */
} esp_display_present_engine_state_t;

/** @brief Engine runtime status snapshot. */
typedef struct {
    esp_display_present_engine_state_t state; /**< Current engine lifecycle state. */
    bool frame_active;                        /**< True while a frame is currently building. */
    bool cancel_pending;                      /**< True while a frame cancellation is draining. */
    size_t pending_frames;                    /**< Number of committed frames still in flight. */
    uint64_t committed_frames;                /**< Total frames committed. */
    uint64_t completed_frames;                /**< Terminal transfers observed (not necessarily successful). */
    esp_err_t first_error;                    /**< Sticky first accepted-operation error (never transient). */
    esp_err_t last_service_error;             /**< Latest pump result, including retryable rejection. */
} esp_display_present_engine_status_t;

#ifdef __cplusplus
}
#endif
