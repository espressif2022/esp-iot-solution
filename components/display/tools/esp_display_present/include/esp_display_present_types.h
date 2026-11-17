/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Clockwise logical-to-physical display rotation in degrees. */
typedef enum {
    ESP_DISPLAY_PRESENT_ROTATE_0 = 0,   /**< No rotation. */
    ESP_DISPLAY_PRESENT_ROTATE_90 = 90, /**< 90 degrees clockwise. */
    ESP_DISPLAY_PRESENT_ROTATE_180 = 180, /**< 180 degrees clockwise. */
    ESP_DISPLAY_PRESENT_ROTATE_270 = 270, /**< 270 degrees clockwise. */
} esp_display_present_rotation_t;

/** @brief Pixel storage format. */
typedef enum {
    ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565 = 0, /**< 16-bit RGB565. */
    ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB888,     /**< 24-bit RGB888. */
} esp_display_present_pixel_format_t;

/**
 * @brief Why a presenter latched a fault.
 *
 * A transfer timeout is a hardware-level condition recoverable by deleting and
 * recreating the presenter; a producer protocol fault is a producer bug with no
 * in-place recovery.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_FAULT_NONE = 0,            /**< No fault recorded. */
    ESP_DISPLAY_PRESENT_FAULT_PRODUCER_PROTOCOL,   /**< Ticket/order violation in producer build or fence protocol. */
    ESP_DISPLAY_PRESENT_FAULT_TRANSFER_TIMEOUT,    /**< Panel transfer exceeded budget (original error ESP_ERR_TIMEOUT). */
    ESP_DISPLAY_PRESENT_FAULT_TRANSFER_FAILED,     /**< Accepted transfer or copy failed for a non-timeout reason. */
} esp_display_present_fault_reason_t;

/** @brief Memory domain of the active pixel storage. */
typedef enum {
    ESP_DISPLAY_PRESENT_MEMORY_NONE = 0, /**< This pool is absent. */
    ESP_DISPLAY_PRESENT_MEMORY_INTERNAL, /**< Internal DMA-capable RAM. */
    ESP_DISPLAY_PRESENT_MEMORY_PSRAM,    /**< External PSRAM. */
    ESP_DISPLAY_PRESENT_MEMORY_MIXED,    /**< Driver framebuffers span both domains. */
    ESP_DISPLAY_PRESENT_MEMORY_UNKNOWN,  /**< Platform cannot identify borrowed storage. */
} esp_display_present_memory_domain_t;

/** @brief Opt-in performance statistics since the last reset (durations in microseconds). */
typedef struct {
    uint64_t frame_samples;        /**< Successful commits with a measured begin. */
    uint64_t frame_p99_us;         /**< Begin-to-commit P99 upper bound (1 ms buckets, max for overflow). */
    uint64_t buffer_waits;         /**< Begin/acquire calls that encountered buffer backpressure. */
    uint64_t buffer_wait_timeouts; /**< Begin/acquire calls that timed out waiting for a buffer. */
    uint64_t buffer_wait_us;       /**< Total time from first buffer denial to call return. */
    uint64_t buffer_wait_max_us;   /**< Maximum single buffer-wait duration in microseconds. */
    bool scanout_available;        /**< RGB/DPI scanout VSYNC counters are supported. */
    uint64_t scanout_vsyncs;       /**< Observed panel VSYNC count. */
    uint64_t scanout_supply_misses; /**< VSYNC intervals with backlog at the previous VSYNC and no confirmed switch. */
} esp_display_presenter_metrics_t;

/** @brief Driver completion feedback granularity. */
typedef enum {
    ESP_DISPLAY_PRESENT_COMPLETION_TRANSFER = 0, /**< All frame transfers completed; no scanout event available. */
    ESP_DISPLAY_PRESENT_COMPLETION_FRAMEBUFFER,  /**< Hardware VSYNC available; confirmed_present_frame tracks display confirmation. */
} esp_display_present_completion_t;

/** @brief Frame and binding identity; rotation changes generation and frame_id=0 means commit failed. */
typedef struct {
    uint64_t frame_id;   /**< Committed frame ID; zero on failure. */
    uint32_t generation; /**< Binding generation that issued this receipt. */
    bool display_tracked; /**< Only tracked receipts use confirmed_present_frame. */
} esp_display_presenter_frame_receipt_t;

/** @brief Coherent frame timeline snapshot; zero means no frame has reached that point. */
typedef struct {
    uint32_t generation;                          /**< Current binding generation. */
    uint64_t last_submitted_frame;                /**< Latest submitted frame ID. */
    uint64_t completed_transfer_frame;            /**< Latest frame with completed transfers. */
    uint64_t completed_present_frame;             /**< Ownership fence; coalesced submissions do not prove every frame was displayed. */
    uint64_t confirmed_present_frame;             /**< Tracked frames only: switch fence plus two later RGB/MIPI VSYNCs. */
    esp_display_present_completion_t completion;  /**< Available driver completion feedback. */
    esp_display_present_fault_reason_t fault_reason; /**< Latched transfer or protocol fault. */
    bool tracks_transfer;                         /**< Separate transfer queue; otherwise transfer follows the present fence. */
    bool accepting_callbacks;                     /**< Whether completion callbacks are currently accepted. */
} esp_display_presenter_frame_status_t;

/**
 * @brief Inclusive logical pixel area (closed interval [x1, x2] x [y1, y2]).
 *
 * Unlike GSP's open-interval gsp_rect_t, x2 and y2 name the last affected pixel.
 */
typedef struct {
    int x1; /**< Left coordinate. */
    int y1; /**< Top coordinate. */
    int x2; /**< Right coordinate. */
    int y2; /**< Bottom coordinate. */
} esp_display_present_area_t;

/**
 * @brief Producer raster-area alignment requirements (0 means no extra alignment).
 */
typedef struct {
    uint16_t x_pixels;      /**< Required X-origin alignment in pixels. */
    uint16_t y_pixels;      /**< Required Y-origin alignment in pixels. */
    uint16_t width_pixels;  /**< Required area-width alignment in pixels. */
    uint16_t height_pixels; /**< Required area-height alignment in pixels. */
} esp_display_present_render_alignment_t;

/**
 * @brief Logical damage supplied when preparing a surface frame.
 */
typedef struct {
    const esp_display_present_area_t *dirty_areas; /**< Logical dirty areas. */
    size_t dirty_area_count; /**< Number of entries in @c dirty_areas. */
    esp_display_present_render_alignment_t render_alignment; /**< Producer raster alignment. */
    bool has_scroll; /**< Whether this request contains a scroll operation. */
    esp_display_present_area_t scroll_viewport; /**< Logical viewport affected by scrolling. */
    int16_t scroll_dx; /**< Horizontal scroll displacement in pixels. */
    int16_t scroll_dy; /**< Vertical scroll displacement in pixels. */
} esp_display_present_surface_request_t;

#ifdef __cplusplus
}
#endif
