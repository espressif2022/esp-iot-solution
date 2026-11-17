/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_draw_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Read-only pixel source descriptor. */
typedef struct {
    const void *pixels;                  /**< Source pixel buffer base pointer. */
    size_t capacity_bytes;               /**< Total readable capacity in bytes. */
    esp_display_present_layout_t layout; /**< Source dimensions, stride, format, and byte order. */
} esp_display_present_source_t;

/** @brief Bounded pixel copy/rotate/byte-swap operation descriptor. */
typedef struct {
    esp_display_present_source_t src;  /**< Read-only source surface. */
    esp_display_present_surface_t dst; /**< Writable destination surface. */
    esp_display_present_rect_t src_area; /**< Source sub-rectangle to copy. */
    uint32_t dst_x;                      /**< Destination X origin in pixels. */
    uint32_t dst_y;                      /**< Destination Y origin in pixels. */
    esp_display_present_engine_rotation_t rotation; /**< Clockwise rotation around @c src_area before placing at dst. */
} esp_display_present_copy_request_t;

/**
 * @brief Optional blocking pixel executor callback.
 *
 * On return, all hardware/CPU reads and writes must have ended even on failure.
 * NULL uses the CPU reference implementation.
 */
typedef esp_err_t (*esp_display_present_pixel_execute_fn)(void *user, const esp_display_present_copy_request_t *request);

#ifdef __cplusplus
}
#endif
