/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_display_present_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Integer 2D coordinate point. */
typedef struct {
    int32_t x; /**< X coordinate in pixels. */
    int32_t y; /**< Y coordinate in pixels. */
} esp_display_present_point_t;

/** @brief Unsigned 2D surface dimensions. */
typedef struct {
    uint16_t width;  /**< Width in pixels. */
    uint16_t height; /**< Height in pixels. */
} esp_display_present_size_t;

/** @brief Return true if inclusive @p area is non-empty and within [0, width) x [0, height). */
static inline bool esp_display_present_geometry_area_is_valid(
    const esp_display_present_area_t *area, uint16_t width, uint16_t height)
{
    return area != NULL && width != 0 && height != 0 && area->x1 >= 0 &&
           area->y1 >= 0 && area->x2 >= area->x1 && area->y2 >= area->y1 &&
           area->x2 < width && area->y2 < height;
}

/**
 * @brief Conservatively expand inclusive logical @p area to @p alignment and clip to @p logical_size.
 *
 * Zero alignment fields mean 1; alignments need not be powers of two.
 */
esp_err_t esp_display_present_geometry_align_area(
    esp_display_present_size_t logical_size,
    const esp_display_present_area_t *area,
    const esp_display_present_render_alignment_t *alignment,
    esp_display_present_area_t *out_area);

/** @brief Map a logical pixel coordinate into the rotated physical panel coordinate space. */
esp_err_t esp_display_present_geometry_map_logical_point_to_physical(
    esp_display_present_rotation_t rotation,
    esp_display_present_size_t logical_size,
    const esp_display_present_point_t *logical_point,
    esp_display_present_point_t *out_physical_point);

/** @brief Map a physical panel coordinate back into the logical render coordinate space. */
esp_err_t esp_display_present_geometry_map_physical_point_to_logical(
    esp_display_present_rotation_t rotation,
    esp_display_present_size_t logical_size,
    const esp_display_present_point_t *physical_point,
    esp_display_present_point_t *out_logical_point);

#ifdef __cplusplus
}
#endif
