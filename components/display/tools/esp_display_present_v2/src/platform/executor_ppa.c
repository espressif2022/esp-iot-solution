/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#ifdef PV2_HOST
#ifndef PV2_HOST_PPA
#define PV2_HOST_PPA 1
#endif
#define PV2_PLUGIN_ENABLED PV2_HOST_PPA
#else
#include "soc/soc_caps.h"
#define PV2_PLUGIN_ENABLED SOC_PPA_SUPPORTED
#endif
#if PV2_PLUGIN_ENABLED
#include "executor_hw.h"
#include "driver/ppa.h"
typedef struct {
    pv2_executor_t base;
    pv2_mutex_t *gate;
    ppa_client_handle_t ppa;
} plugin_t;
static esp_err_t caps(void *context, pv2_executor_caps_t *out)
{
    (void)context;
    *out = (pv2_executor_caps_t) {
        .name = "ppa", .format_mask = 3,
        .rotation_mask = 15, .byte_swap = true,
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
    if (dma2d_get_buffer_alignment_constraint(r->source.pixels) > 1 ||
            dma2d_get_buffer_alignment_constraint(r->destination.pixels) > 1) {
        return unsupported(reason, PV2_EXEC_MEMORY);
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
    esp_err_t error = ppa_unregister_client(a->ppa);
    if (error != ESP_OK) {
        return error;
    }
    pv2_mutex_destroy(a->gate);
    pv2_free(a);
    return ESP_OK;
}
static const pv2_executor_ops_t ops = {caps, probe, execute, destroy};
esp_err_t pv2_executor_ppa_create(pv2_executor_t **out)
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
    esp_err_t error;
    ppa_client_config_t cfg = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    error = ppa_register_client(&cfg, &a->ppa);
    if (error != ESP_OK) {
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
esp_err_t pv2_executor_ppa_create(pv2_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
