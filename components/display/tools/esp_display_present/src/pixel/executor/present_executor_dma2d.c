/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "pixel/present_executor.h"
#ifdef PRESENT_HOST
#ifndef PRESENT_HOST_DMA2D
#define PRESENT_HOST_DMA2D 1
#endif
#define PRESENT_PLUGIN_ENABLED PRESENT_HOST_DMA2D
#else
#include "soc/soc_caps.h"
#define PRESENT_PLUGIN_ENABLED (SOC_DMA2D_SUPPORTED && (PRESENT_IDF_ASYNC_DMA2D_API || PRESENT_IDF_COLOR_CONVERT_DMA2D_API))
#endif
#if PRESENT_PLUGIN_ENABLED
#include "pixel/executor/present_executor_hw.h"
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
#include "esp_async_color_convert.h"
typedef async_color_convert_handle_t dma_handle_t;
typedef async_color_convert_event_data_t dma_event_t;
#else
#include "esp_private/async_memcpy_dma2d.h"
typedef async_memcpy_dma2d_handle_t dma_handle_t;
typedef async_memcpy_dma2d_event_data_t dma_event_t;
#endif
typedef struct {
    esp_display_present_executor_t base;
    present_mutex_t *gate;
    present_os_event_t *done;
    dma_handle_t dma;
} plugin_t;
static bool PRESENT_ISR_ATTR dma2d_completed(dma_handle_t driver, dma_event_t *event, void *context)
{
    (void)driver;
    (void)event;
    plugin_t *plugin = context;
    return present_event_signal(plugin->done, true);
}
static esp_err_t dma2d_caps(void *context, esp_display_present_executor_caps_t *out)
{
    (void)context;
    *out = (esp_display_present_executor_caps_t) {
        .name = "dma2d",
        .format_mask = 3,
        .rotation_mask = 1,
        .byte_swap = false,
        .base_alignment = 64,
        .dst_cap_align = 64,
        .stride_align = 1,
        .max_stride_pixels = 0x3fff,
        .max_height = 0x3fff,
        .addr_constraints = true,
        .full_stride_cap = true
    };
    return ESP_OK;
}
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
/* The public driver invalidates the entire declared destination picture and
 * requires aligned base and length. Keep real row strides while widening to
 * the nearest cache-aligned row boundaries, within the borrowed allocation. */
static bool dma2d_destination_window(const esp_display_present_copy_request_t *request,
                                     size_t *first_row, size_t *rows)
{
    size_t alignment = esp_cache_get_line_size_by_addr(request->dst.pixels);
    if (alignment < 64) {
        alignment = 64;
    }
    size_t stride = request->dst.layout.stride_bytes;
    size_t a = stride, b = alignment;
    while (b) {
        size_t remainder = a % b;
        a = b;
        b = remainder;
    }
    size_t row_group = alignment / a;
    size_t start = request->dst_y - request->dst_y % row_group;
    size_t end = request->dst_y + request->src_area.height;
    end += (row_group - end % row_group) % row_group;
    if (end > request->dst.capacity_bytes / stride || end - start > 0x3fff) {
        return false;
    }
    *first_row = start;
    *rows = end - start;
    return true;
}
#endif
static esp_err_t dma2d_probe(void *context, const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason)
{
    (void)context;
    void *band;
    size_t bytes;
    esp_err_t error = hw_probe(request, reason, &band, &bytes);
    if (error != ESP_OK) {
        return error;
    }
    if (request->rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || request->src.layout.byte_order != request->dst.layout.byte_order) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_TRANSFORM);
    }
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
    size_t first_row = 0, rows = 0;
    if (!dma2d_destination_window(request, &first_row, &rows)) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_CAPACITY);
    }
#endif
    size_t bpp = request->src.layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t si = hw_memory_alignment(request->src.pixels);
    size_t di = hw_memory_alignment(request->dst.pixels);
    if (si == SIZE_MAX || di == SIZE_MAX) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_MEMORY);
    }
    if ((si > 1 && ((uintptr_t)request->src.pixels % si || request->src.layout.stride_bytes % si ||
                    (request->src_area.x * bpp) % si || (request->src_area.width * bpp) % si)) ||
            (di > 1 && ((uintptr_t)request->dst.pixels % di || request->dst.layout.stride_bytes % di ||
                        (request->dst_x * bpp) % di || (request->src_area.width * bpp) % di))) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_ALIGNMENT);
    }
    return ESP_OK;
}
static esp_err_t dma2d_execute(void *context, const esp_display_present_copy_request_t *request)
{
    plugin_t *plugin = context;
    esp_display_present_executor_reason_t reason;
    esp_err_t error = dma2d_probe(context, request, &reason);
    if (error != ESP_OK) {
        return error;
    }
    void *band;
    size_t band_bytes;
    error = hw_probe(request, &reason, &band, &band_bytes);
    if (error != ESP_OK) {
        return error;
    }
    size_t bpp = request->src.layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    (void)present_mutex_take(plugin->gate, UINT32_MAX);
    error = hw_sync_source(request);
    if (error != ESP_OK) {
        goto done;
    }
    error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (error != ESP_OK) {
        goto done;
    }
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
    /* This driver synchronizes the declared pictures. Rebase both pictures
     * to the affected rows so a tile does not synchronize an entire FB.
     * Strides and X offsets stay intact; hw_probe checked allocation edges. */
    size_t first_row = 0, rows = 0;
    (void)dma2d_destination_window(request, &first_row, &rows); /* Verified by probe. */
    async_color_convert_request_t op = {
        .src_buffer = (const uint8_t *)request->src.pixels + request->src_area.y * request->src.layout.stride_bytes,
        .src_stride = request->src.layout.stride_bytes / bpp,
        .src_height = request->src_area.height,
        .src_x = request->src_area.x,
        .dst_buffer = (uint8_t *)request->dst.pixels + first_row * request->dst.layout.stride_bytes,
        .dst_stride = request->dst.layout.stride_bytes / bpp,
        .dst_height = rows,
        .dst_x = request->dst_x,
        .dst_y = request->dst_y - first_row,
        .copy_width = request->src_area.width,
        .copy_height = request->src_area.height,
        .src_color_format = bpp == 2 ? ESP_COLOR_FOURCC_RGB16 : ESP_COLOR_FOURCC_BGR24,
        .dst_color_format = bpp == 2 ? ESP_COLOR_FOURCC_RGB16 : ESP_COLOR_FOURCC_BGR24,
    };
#else
    async_memcpy_dma2d_trans_desc_t op = {.src_buffer = request->src.pixels,
                                          .src_stride = request->src.layout.stride_bytes / bpp,
                                          .src_height = request->src.layout.height,
                                          .src_x = request->src_area.x,
                                          .src_y = request->src_area.y,
                                          .dst_buffer = request->dst.pixels,
                                          .dst_stride = request->dst.layout.stride_bytes / bpp,
                                          .dst_height = request->dst.layout.height,
                                          .dst_x = request->dst_x,
                                          .dst_y = request->dst_y,
                                          .copy_width = request->src_area.width,
                                          .copy_height = request->src_area.height,
                                          .pixel_format =
                                              bpp == 2 ? ESP_COLOR_FOURCC_RGB16 : ESP_COLOR_FOURCC_BGR24
                                         };
#endif
    (void)present_event_wait(plugin->done, 0);
    /* IDF signals our ISR callback before recycling its descriptor. A
     * following worker may arrive in that brief window. INVALID_STATE is
     * documented definite rejection (no free transaction), safe to retry. */
    uint64_t start = present_now_ms();
    do {
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
        error = esp_async_color_convert(plugin->dma, &op, dma2d_completed, plugin);
#else
        error = esp_async_memcpy_dma2d(plugin->dma, &op, dma2d_completed, plugin);
#endif
        if (error != ESP_ERR_INVALID_STATE || present_expired(start, 1000)) {
            break;
        }
        present_pause();
    } while (true);
    if (error == ESP_OK) {
        (void)present_event_wait(plugin->done, UINT32_MAX);
    }
    if (error == ESP_OK) {
        error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
done:
    present_mutex_give(plugin->gate);
    return error;
}
static esp_err_t dma2d_destroy(void *context)
{
    plugin_t *plugin = context;
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
    esp_err_t error = esp_async_color_convert_uninstall(plugin->dma);
#else
    esp_err_t error = esp_async_memcpy_uninstall_dma2d(plugin->dma);
#endif
    if (error != ESP_OK) {
        return error;
    }
    present_event_destroy(plugin->done);
    present_mutex_destroy(plugin->gate);
    present_free(plugin);
    return ESP_OK;
}
static const esp_display_present_executor_ops_t dma2d_ops = {dma2d_caps, dma2d_probe, dma2d_execute, dma2d_destroy};
esp_err_t esp_display_present_executor_dma2d_create(esp_display_present_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    plugin_t *plugin = present_alloc(sizeof(*plugin));
    if (!plugin) {
        return ESP_ERR_NO_MEM;
    }
    plugin->gate = present_mutex_create();
    if (!plugin->gate) {
        present_free(plugin);
        return ESP_ERR_NO_MEM;
    }
    plugin->done = present_event_create();
    if (!plugin->done) {
        present_mutex_destroy(plugin->gate);
        present_free(plugin);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error;
#if PRESENT_IDF_COLOR_CONVERT_DMA2D_API
    async_color_convert_config_t cfg = {.backlog = 1, .dma_burst_size = 128};
    error = esp_async_color_convert_install_dma2d(&cfg, &plugin->dma);
#else
    async_memcpy_dma2d_config_t cfg = {.backlog = 1};
    error = esp_async_memcpy_install_dma2d(&cfg, &plugin->dma);
#endif
    if (error != ESP_OK) {
        present_event_destroy(plugin->done);
        present_mutex_destroy(plugin->gate);
        present_free(plugin);
        return error;
    }
    plugin->base = (esp_display_present_executor_t) {
        &dma2d_ops, plugin
    };
    *out = &plugin->base;
    return ESP_OK;
}
#else
esp_err_t esp_display_present_executor_dma2d_create(esp_display_present_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
