/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_ops.h"
#include "pixel/present_executor.h"
#include "platform/present_os.h"
#include "esp_cache.h"
#ifndef PRESENT_IDF_DMA2D_ALIGNMENT_API
#define PRESENT_IDF_DMA2D_ALIGNMENT_API 1
#endif
#if PRESENT_IDF_DMA2D_ALIGNMENT_API
#include "esp_private/dma2d.h"
#else
#include "esp_memory_utils.h"
#ifndef PRESENT_IDF_EFUSE_ENCRYPTION_API
#define PRESENT_IDF_EFUSE_ENCRYPTION_API 1
#endif
#if PRESENT_IDF_EFUSE_ENCRYPTION_API
#include "esp_efuse.h"
#else
#include "esp_flash_encrypt.h"
#endif
#endif

/** @brief Query required MSPI/DMA alignment constraint for @p pixels (SIZE_MAX when unsupported). */
static inline size_t hw_memory_alignment(const void *pixels)
{
#if PRESENT_IDF_DMA2D_ALIGNMENT_API
    return dma2d_get_buffer_alignment_constraint(pixels);
#else
    if (esp_ptr_internal(pixels)) {
        return 1;
    }
    if (!esp_ptr_external_ram(pixels)) {
        return SIZE_MAX;
    }
#if CONFIG_SPIRAM_ECC_ENABLE
    return SIZE_MAX;
#elif PRESENT_IDF_EFUSE_ENCRYPTION_API
    return esp_efuse_is_flash_encryption_enabled() ? SIZE_MAX : 1;
#else
    return esp_flash_encryption_enabled() ? SIZE_MAX : 1;
#endif
#endif
}
/** @brief Compute aligned destination cache-sync band covering transformed @p request rows. */
static bool destination_band(const esp_display_present_copy_request_t *request, size_t alignment,
                             void **address, size_t *bytes)
{
    bool quarter = request->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || request->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270;
    size_t rows = quarter ? request->src_area.width : request->src_area.height;
    size_t stride = request->dst.layout.stride_bytes;
    size_t capacity = request->dst.capacity_bytes;
    if (!alignment || !stride || request->dst_y > capacity / stride) {
        return false;
    }
    size_t start = request->dst_y * stride;
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
    *address = (uint8_t *)request->dst.pixels + start;
    *bytes = end - start;
    return true;
}

static inline esp_err_t hw_sync(void *pixels, size_t bytes, int flags)
{
    esp_err_t e = esp_cache_msync(pixels, bytes, flags);
    return e == ESP_ERR_NOT_SUPPORTED ? ESP_OK : e;
}

/** @brief Writeback cache lines for declared source rows without reading outside surface bounds. */
static inline esp_err_t hw_sync_source(const esp_display_present_copy_request_t *request)
{
    size_t stride = request->src.layout.stride_bytes;
    size_t start = request->src_area.y * stride;
    size_t bytes = request->src_area.height * stride;
    return hw_sync((uint8_t *)request->src.pixels + start, bytes,
                   ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}
static inline esp_err_t unsupported(esp_display_present_executor_reason_t *reason, esp_display_present_executor_reason_t why)
{
    *reason = why;
    return ESP_ERR_NOT_SUPPORTED;
}
/** @brief Common pre-execution probe checks shared by DMA2D and PPA hardware executors. */
static inline esp_err_t hw_probe(const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason,
                                 void **band, size_t *band_bytes)
{
    *reason = ESP_DISPLAY_PRESENT_EXEC_INVALID_REQUEST;
    esp_err_t e = esp_display_present_pixel_validate_request(request);
    if (e != ESP_OK) {
        return e;
    }
    size_t bpp = request->src.layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t sa = esp_cache_get_line_size_by_addr(request->src.pixels);
    size_t da = esp_cache_get_line_size_by_addr(request->dst.pixels);
    if (sa < 64) {
        sa = 64;
    }
    if (da < 64) {
        da = 64;
    }
    if (request->src.pixels == request->dst.pixels) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_IN_PLACE);
    }
    if ((uintptr_t)request->src.pixels % sa || (uintptr_t)request->dst.pixels % da) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_ALIGNMENT);
    }
    if (!request->dst.capacity_bytes || request->dst.capacity_bytes % da || request->dst.capacity_bytes > UINT32_MAX) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_CAPACITY);
    }
    if (request->src.layout.stride_bytes > UINT32_MAX || request->dst.layout.stride_bytes > UINT32_MAX ||
            request->src.layout.height > request->src.capacity_bytes / request->src.layout.stride_bytes ||
            request->dst.layout.height > request->dst.capacity_bytes / request->dst.layout.stride_bytes ||
            request->src.layout.stride_bytes / bpp > 0x3fff || request->src.layout.height > 0x3fff ||
            request->dst.layout.stride_bytes / bpp > 0x3fff || request->dst.layout.height > 0x3fff) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_LAYOUT);
    }
    if (!destination_band(request, da, band, band_bytes)) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_CAPACITY);
    }
    *reason = ESP_DISPLAY_PRESENT_EXEC_SUPPORTED;
    return ESP_OK;
}
