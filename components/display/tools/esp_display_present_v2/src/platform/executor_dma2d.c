/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#ifdef PV2_HOST
#ifndef PV2_HOST_DMA2D
#define PV2_HOST_DMA2D 1
#endif
#define PV2_PLUGIN_ENABLED PV2_HOST_DMA2D
#else
#include "soc/soc_caps.h"
#define PV2_PLUGIN_ENABLED SOC_DMA2D_SUPPORTED
#endif
#if PV2_PLUGIN_ENABLED
#include "executor_hw.h"
#include "esp_private/async_memcpy_dma2d.h"
typedef struct {
    pv2_executor_t base;
    pv2_mutex_t *gate;
    pv2_event_t *done;
    async_memcpy_dma2d_handle_t dma;
} plugin_t;
static bool PV2_ISR_ATTR completed(async_memcpy_dma2d_handle_t driver, async_memcpy_dma2d_event_data_t *event, void *context)
{
    (void)driver;
    (void)event;
    plugin_t *a = context;
    return pv2_event_signal(a->done, true);
}
static esp_err_t caps(void *context, pv2_executor_caps_t *out)
{
    (void)context;
    *out = (pv2_executor_caps_t) {
        .name = "dma2d", .format_mask = 3,
        .rotation_mask = 1, .byte_swap = false,
        .base_alignment = 64, .destination_capacity_alignment = 64, .stride_alignment_bytes = 1,
        .max_stride_pixels = 0x3fff, .max_height = 0x3fff,
        .address_dependent_constraints = true, .requires_full_stride_capacity = true
    };
    return ESP_OK;
}
static esp_err_t probe(void *context, const pv2_copy_request_t *r, pv2_executor_reason_t *reason)
{
    (void)context;
    void *band;
    size_t bytes;
    esp_err_t error = hw_probe(r, reason, &band, &bytes);
    if (error != ESP_OK) {
        return error;
    }
    if (r->rotation != PV2_ROTATE_0 || r->source.layout.byte_order != r->destination.layout.byte_order) {
        return unsupported(reason, PV2_EXEC_TRANSFORM);
    }
    size_t bpp = r->source.layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t si = dma2d_get_buffer_alignment_constraint(r->source.pixels);
    size_t di = dma2d_get_buffer_alignment_constraint(r->destination.pixels);
    if ((si > 1 && ((uintptr_t)r->source.pixels % si || r->source.layout.stride_bytes % si ||
                    (r->source_area.x * bpp) % si || (r->source_area.width * bpp) % si)) ||
            (di > 1 && ((uintptr_t)r->destination.pixels % di || r->destination.layout.stride_bytes % di ||
                        (r->destination_x * bpp) % di || (r->source_area.width * bpp) % di))) {
        return unsupported(reason, PV2_EXEC_ALIGNMENT);
    }
    return ESP_OK;
}
static esp_err_t execute(void *context, const pv2_copy_request_t *r)
{
    plugin_t *a = context;
    pv2_executor_reason_t reason;
    esp_err_t error = probe(context, r, &reason);
    if (error != ESP_OK) {
        return error;
    }
    void *band;
    size_t band_bytes;
    error = hw_probe(r, &reason, &band, &band_bytes);
    if (error != ESP_OK) {
        return error;
    }
    size_t bpp = r->source.layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    (void)pv2_mutex_take(a->gate, UINT32_MAX);
    error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (error != ESP_OK) {
        goto done;
    }
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
    if (error == ESP_OK) {
        error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
done:
    pv2_mutex_give(a->gate);
    return error;
}
static esp_err_t destroy(void *context)
{
    plugin_t *a = context;
    esp_err_t error = esp_async_memcpy_uninstall_dma2d(a->dma);
    if (error != ESP_OK) {
        return error;
    }
    pv2_event_destroy(a->done);
    pv2_mutex_destroy(a->gate);
    pv2_free(a);
    return ESP_OK;
}
static const pv2_executor_ops_t ops = {caps, probe, execute, destroy};
esp_err_t pv2_executor_dma2d_create(pv2_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    plugin_t *a = pv2_alloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    a->gate = pv2_mutex_create();
    if (!a->gate) {
        pv2_free(a);
        return ESP_ERR_NO_MEM;
    }
    a->done = pv2_event_create();
    if (!a->done) {
        pv2_mutex_destroy(a->gate);
        pv2_free(a);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error;
    async_memcpy_dma2d_config_t cfg = {.backlog = 1};
    error = esp_async_memcpy_install_dma2d(&cfg, &a->dma);
    if (error != ESP_OK) {
        pv2_event_destroy(a->done);
        pv2_mutex_destroy(a->gate);
        pv2_free(a);
        return error;
    }
    a->base = (pv2_executor_t) {
        &ops, a
    };
    *out = &a->base;
    return ESP_OK;
}
#else
esp_err_t pv2_executor_dma2d_create(pv2_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
