/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_executor.h"
#include "present_os.h"
#include "esp_cache.h"
#include "esp_private/dma2d.h"
/* Match the driver's extended destination rows, including quarter-turn height.
 * The allocation is exclusively borrowed; aligned edge lines remain inside it. */
static bool destination_band(const esp_display_present_copy_request_t *request, size_t alignment,
                             void **address, size_t *bytes)
{
    bool quarter = request->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || request->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270;
    size_t rows = quarter ? request->source_area.width : request->source_area.height;
    size_t stride = request->destination.layout.stride_bytes;
    size_t capacity = request->destination.capacity_bytes;
    if (!alignment || !stride || request->destination_y > capacity / stride) {
        return false;
    }
    size_t start = request->destination_y * stride;
    if (rows > (capacity - start) / stride) {
        return false;
    }
    size_t end = start + rows * stride;
    size_t padding = (alignment - end % alignment) % alignment;
    if (padding > capacity - end) {
        return false;
    }
    start -= start % alignment;
    end += padding;
    *address = (uint8_t *)request->destination.pixels + start;
    *bytes = end - start;
    return true;
}

static inline esp_err_t hw_sync(void *pixels, size_t bytes, int flags)
{
    esp_err_t e = esp_cache_msync(pixels, bytes, flags);
    return e == ESP_ERR_NOT_SUPPORTED ? ESP_OK : e;
}
static inline esp_err_t unsupported(esp_display_present_executor_reason_t *reason, esp_display_present_executor_reason_t why)
{
    *reason = why;
    return ESP_ERR_NOT_SUPPORTED;
}
static inline esp_err_t hw_probe(const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason,
                                 void **band, size_t *band_bytes)
{
    *reason = ESP_DISPLAY_PRESENT_EXEC_INVALID_REQUEST;
    esp_err_t e = esp_display_present_pixel_validate_request(request);
    if (e != ESP_OK) {
        return e;
    }
    size_t bpp = request->source.layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t sa = esp_cache_get_line_size_by_addr(request->source.pixels);
    size_t da = esp_cache_get_line_size_by_addr(request->destination.pixels);
    if (sa < 64) {
        sa = 64;
    }
    if (da < 64) {
        da = 64;
    }
    if (request->source.pixels == request->destination.pixels) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_IN_PLACE);
    }
    if ((uintptr_t)request->source.pixels % sa || (uintptr_t)request->destination.pixels % da) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_ALIGNMENT);
    }
    if (!request->destination.capacity_bytes || request->destination.capacity_bytes % da || request->destination.capacity_bytes > UINT32_MAX) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_CAPACITY);
    }
    if (request->source.layout.stride_bytes > UINT32_MAX || request->destination.layout.stride_bytes > UINT32_MAX ||
            request->source.layout.height > request->source.capacity_bytes / request->source.layout.stride_bytes ||
            request->destination.layout.height > request->destination.capacity_bytes / request->destination.layout.stride_bytes ||
            request->source.layout.stride_bytes / bpp > 0x3fff || request->source.layout.height > 0x3fff ||
            request->destination.layout.stride_bytes / bpp > 0x3fff || request->destination.layout.height > 0x3fff) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_LAYOUT);
    }
    if (!destination_band(request, da, band, band_bytes)) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_CAPACITY);
    }
    *reason = ESP_DISPLAY_PRESENT_EXEC_SUPPORTED;
    return ESP_OK;
}
