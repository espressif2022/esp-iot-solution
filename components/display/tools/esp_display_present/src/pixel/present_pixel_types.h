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

typedef struct {
    const void *pixels;
    size_t capacity_bytes;
    esp_display_present_layout_t layout;
} esp_display_present_source_t;

typedef struct {
    esp_display_present_source_t src;
    esp_display_present_surface_t dst;
    esp_display_present_rect_t src_area;
    uint32_t dst_x, dst_y;
    esp_display_present_engine_rotation_t rotation; /* Clockwise around src_area, then translate. */
} esp_display_present_copy_request_t;

/** Optional blocking executor, called only by the engine worker. On return
 * ALL reads/writes must have ended, even on failure. Hardware implementations
 * must drain/abort safely before returning; an uncertain DMA error cannot be
 * returned as terminal failure while hardware still owns memory. Borrowed
 * context stays valid until successful stop. NULL executes the CPU reference. */
typedef esp_err_t (*esp_display_present_pixel_execute_fn)(void *user, const esp_display_present_copy_request_t *request);

#ifdef __cplusplus
}
#endif
