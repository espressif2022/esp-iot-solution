/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_pixel_accel.h"
#include "present_os.h"
#include "esp_cache.h"
#ifdef PV2_HOST
#define PV2_HAS_PPA 1
#define PV2_HAS_DMA2D 1
#else
#include "soc/soc_caps.h"
#define PV2_HAS_PPA SOC_PPA_SUPPORTED
#define PV2_HAS_DMA2D SOC_DMA2D_SUPPORTED
#endif
#if PV2_HAS_PPA || PV2_HAS_DMA2D
#include "esp_private/dma2d.h"
#endif
#if PV2_HAS_PPA
#include "driver/ppa.h"
#endif
#if PV2_HAS_DMA2D
#include "esp_private/async_memcpy_dma2d.h"
#endif
struct pv2_pixel_accel {
    pv2_pixel_accel_config_t config;
    pv2_mutex_t *gate;
    pv2_event_t *done;
#if PV2_HAS_PPA
    ppa_client_handle_t ppa;
#endif
#if PV2_HAS_DMA2D
    async_memcpy_dma2d_handle_t dma;
#endif
};
#if PV2_HAS_DMA2D
static bool PV2_ISR_ATTR completed(async_memcpy_dma2d_handle_t driver, async_memcpy_dma2d_event_data_t *event,
                                   void *user)
{
    (void)driver;
    (void)event;
    pv2_pixel_accel_t *a = user;
    return pv2_event_signal(a->done, true);
}
#endif
static esp_err_t sync_memory(void *pixels, size_t bytes, int flags)
{
    esp_err_t error = esp_cache_msync(pixels, bytes, flags);
    return error == ESP_ERR_NOT_SUPPORTED ? ESP_OK : error;
}

esp_err_t pv2_pixel_accel_create(const pv2_pixel_accel_config_t *config, pv2_pixel_accel_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || config->kind < PV2_ACCEL_DMA2D || config->kind > PV2_ACCEL_PPA) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_pixel_accel_t *a = pv2_alloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    a->config = *config;
    a->gate = pv2_mutex_create();
    a->done = pv2_event_create();
    esp_err_t error = ESP_ERR_NO_MEM;
    if (!a->gate || !a->done) {
        goto fail;
    }
    error = ESP_ERR_NOT_SUPPORTED;
#if PV2_HAS_PPA
    if (config->kind == PV2_ACCEL_PPA) {
        ppa_client_config_t pc = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
        error = ppa_register_client(&pc, &a->ppa);
    }
#endif
#if PV2_HAS_DMA2D
    if (config->kind == PV2_ACCEL_DMA2D) {
        async_memcpy_dma2d_config_t dc = {.backlog = 1};
        error = esp_async_memcpy_install_dma2d(&dc, &a->dma);
    }
#endif
    if (error != ESP_OK) {
        goto fail;
    }
    *out = a;
    return ESP_OK;
fail:
    pv2_event_destroy(a->done);
    pv2_mutex_destroy(a->gate);
    pv2_free(a);
    return error;
}
/* Match the driver's extended destination rows, including quarter-turn height.
 * The allocation is exclusively borrowed; aligned edge lines remain inside it. */
static bool destination_band(const pv2_copy_request_t *request, size_t alignment,
                             void **address, size_t *bytes)
{
    bool quarter = request->rotation == PV2_ROTATE_90 || request->rotation == PV2_ROTATE_270;
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

esp_err_t pv2_pixel_accel_execute(void *context, const pv2_copy_request_t *r)
{
    pv2_pixel_accel_t *a = context;
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_pixel_validate_request(r);
    if (error != ESP_OK) {
        return error;
    }
    size_t bpp = r->source.layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    (void)bpp;
    size_t sa = esp_cache_get_line_size_by_addr(r->source.pixels);
    size_t da = esp_cache_get_line_size_by_addr(r->destination.pixels);
    if (sa < 64) {
        sa = 64;
    }
    if (da < 64) {
        da = 64;
    }
    bool compatible = true;
    if (r->source.pixels == r->destination.pixels || (uintptr_t)r->source.pixels % sa ||
            (uintptr_t)r->destination.pixels % da) {
        compatible = false;
    }
    if (!r->destination.capacity_bytes || r->destination.capacity_bytes % da ||
            r->destination.capacity_bytes > UINT32_MAX) {
        compatible = false;
    }
    if (r->source.layout.stride_bytes > UINT32_MAX || r->destination.layout.stride_bytes > UINT32_MAX ||
            r->source.layout.height > r->source.capacity_bytes / r->source.layout.stride_bytes ||
            r->destination.layout.height > r->destination.capacity_bytes / r->destination.layout.stride_bytes) {
        compatible = false;
    }
    if (a->config.kind == PV2_ACCEL_DMA2D &&
            (r->rotation != PV2_ROTATE_0 || r->source.layout.byte_order != r->destination.layout.byte_order)) {
        compatible = false;
    }
#if PV2_HAS_PPA || PV2_HAS_DMA2D
    size_t si = dma2d_get_buffer_alignment_constraint(r->source.pixels);
    size_t di = dma2d_get_buffer_alignment_constraint(r->destination.pixels);
    /* PPA SRM macro-block accesses cannot satisfy strict MSPI alignment. */
    if (a->config.kind == PV2_ACCEL_PPA && (si > 1 || di > 1)) {
        compatible = false;
    }
    if (a->config.kind == PV2_ACCEL_DMA2D) {
        if ((si > 1 && ((uintptr_t)r->source.pixels % si || r->source.layout.stride_bytes % si || (r->source_area.x * bpp) % si ||
                        (r->source_area.width * bpp) % si)) ||
                (di > 1 && ((uintptr_t)r->destination.pixels % di || r->destination.layout.stride_bytes % di || (r->destination_x * bpp) % di ||
                            (r->source_area.width * bpp) % di))) {
            compatible = false;
        }
    }
    /* Audited local IDF DMA2D descriptors have 14-bit dimensions. */
    if (r->source.layout.stride_bytes / bpp > 0x3fff || r->source.layout.height > 0x3fff ||
            r->destination.layout.stride_bytes / bpp > 0x3fff || r->destination.layout.height > 0x3fff) {
        compatible = false;
    }
#endif
    if (!compatible) {
        return a->config.cpu_fallback ? pv2_pixel_execute_cpu(r) : ESP_ERR_NOT_SUPPORTED;
    }
    void *band;
    size_t band_bytes;
    if (!destination_band(r, da, &band, &band_bytes)) {
        return a->config.cpu_fallback ? pv2_pixel_execute_cpu(r) : ESP_ERR_NOT_SUPPORTED;
    }
    (void)pv2_mutex_take(a->gate, UINT32_MAX);
    error = sync_memory(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (error != ESP_OK) {
        goto done;
    }
#if PV2_HAS_PPA
    if (a->config.kind == PV2_ACCEL_PPA) {
        ppa_srm_color_mode_t format = bpp == 2 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888;
        const ppa_srm_rotation_angle_t angles[] = {PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_270,
                                                   PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_90
                                                  };
        ppa_srm_oper_config_t op = {.in = {.buffer = r->source.pixels,
                                           .pic_w = r->source.layout.stride_bytes / bpp,
                                           .pic_h = r->source.layout.height,
                                           .block_w = r->source_area.width,
                                           .block_h = r->source_area.height,
                                           .block_offset_x = r->source_area.x,
                                           .block_offset_y = r->source_area.y,
                                           .srm_cm = format
                                          },
                                    .out = {.buffer = r->destination.pixels,
                                            .buffer_size = r->destination.capacity_bytes,
                                            .pic_w = r->destination.layout.stride_bytes / bpp,
                                            .pic_h = r->destination.layout.height,
                                            .block_offset_x = r->destination_x,
                                            .block_offset_y = r->destination_y,
                                            .srm_cm = format
                                           },
                                    .rotation_angle = angles[r->rotation],
                                    .scale_x = 1,
                                    .scale_y = 1,
                                    .byte_swap = r->source.layout.byte_order != r->destination.layout.byte_order,
                                    .mode = PPA_TRANS_MODE_BLOCKING
                                   };
        error = ppa_do_scale_rotate_mirror(a->ppa, &op);
    }
#endif
#if PV2_HAS_DMA2D
    if (a->config.kind == PV2_ACCEL_DMA2D) {
        async_memcpy_dma2d_trans_desc_t op = {.src_buffer = r->source.pixels,
                                              .src_stride = r->source.layout.stride_bytes / bpp,
                                              .src_height = r->source.layout.height,
                                              .src_x = r->source_area.x,
                                              .src_y = r->source_area.y,
                                              .dst_buffer = r->destination.pixels,
                                              .dst_stride = r->destination.layout.stride_bytes / bpp,
                                              .dst_height = r->destination.layout.height,
                                              .dst_x = r->destination_x,
                                              .dst_y = r->destination_y,
                                              .copy_width = r->source_area.width,
                                              .copy_height = r->source_area.height,
                                              .pixel_format =
                                                  bpp == 2 ? ESP_COLOR_FOURCC_RGB16 : ESP_COLOR_FOURCC_BGR24
                                             };
        (void)pv2_event_wait(a->done, 0);
        /* IDF signals our ISR callback before recycling its descriptor. A
         * following worker may arrive in that brief window. INVALID_STATE is
         * documented definite rejection (no free transaction), safe to retry. */
        uint64_t start = pv2_now_ms();
        do {
            error = esp_async_memcpy_dma2d(a->dma, &op, completed, a);
            if (error != ESP_ERR_INVALID_STATE || pv2_expired(start, 1000)) {
                break;
            }
            pv2_pause();
        } while (true);
        if (error == ESP_OK) {
            (void)pv2_event_wait(a->done, UINT32_MAX);
        }
    }
#endif
    if (error == ESP_OK) {
        error = sync_memory(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
done:
    pv2_mutex_give(a->gate);
    return error;
}

esp_err_t pv2_pixel_accel_destroy(pv2_pixel_accel_t *a)
{
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PV2_HAS_PPA
    if (a->config.kind == PV2_ACCEL_PPA) {
        error = ppa_unregister_client(a->ppa);
    }
#endif
#if PV2_HAS_DMA2D
    if (a->config.kind == PV2_ACCEL_DMA2D) {
        error = esp_async_memcpy_uninstall_dma2d(a->dma);
    }
#endif
    if (error != ESP_OK) {
        return error;
    }
    pv2_event_destroy(a->done);
    pv2_mutex_destroy(a->gate);
    pv2_free(a);
    return ESP_OK;
}
