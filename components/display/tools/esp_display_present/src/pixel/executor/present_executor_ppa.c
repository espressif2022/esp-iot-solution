/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#ifdef PRESENT_HOST
#ifndef PRESENT_HOST_PPA
#define PRESENT_HOST_PPA 1
#endif
#define PRESENT_PLUGIN_ENABLED PRESENT_HOST_PPA
#else
#include "soc/soc_caps.h"
#define PRESENT_PLUGIN_ENABLED SOC_PPA_SUPPORTED
#endif
#if PRESENT_PLUGIN_ENABLED
#include "present_executor_hw.h"
#include "driver/ppa.h"
typedef struct {
    esp_display_present_executor_t base;
    present_mutex_t *gate;
    ppa_client_handle_t ppa;
} plugin_t;
static esp_err_t ppa_caps(void *context, esp_display_present_executor_caps_t *out)
{
    (void)context;
    *out = (esp_display_present_executor_caps_t) {
        .name = "ppa", .format_mask = 3,
        .rotation_mask = 15, .byte_swap = true,
        .base_alignment = 64, .destination_capacity_alignment = 64, .stride_alignment_bytes = 1,
        .max_stride_pixels = 0x3fff, .max_height = 0x3fff,
        .address_dependent_constraints = true, .requires_full_stride_capacity = true
    };
    return ESP_OK;
}
static esp_err_t ppa_probe(void *context, const esp_display_present_copy_request_t *request, esp_display_present_executor_reason_t *reason)
{
    (void)context;
    void *band;
    size_t bytes;
    esp_err_t error = hw_probe(request, reason, &band, &bytes);
    if (error != ESP_OK) {
        return error;
    }
    if (dma2d_get_buffer_alignment_constraint(request->source.pixels) > 1 ||
            dma2d_get_buffer_alignment_constraint(request->destination.pixels) > 1) {
        return unsupported(reason, ESP_DISPLAY_PRESENT_EXEC_MEMORY);
    }
    return ESP_OK;
}
static esp_err_t ppa_execute(void *context, const esp_display_present_copy_request_t *request)
{
    plugin_t *plugin = context;
    esp_display_present_executor_reason_t reason;
    esp_err_t error = ppa_probe(context, request, &reason);
    if (error != ESP_OK) {
        return error;
    }
    void *band;
    size_t band_bytes;
    error = hw_probe(request, &reason, &band, &band_bytes);
    if (error != ESP_OK) {
        return error;
    }
    size_t bpp = request->source.layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    (void)present_mutex_take(plugin->gate, UINT32_MAX);
    error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (error != ESP_OK) {
        goto done;
    }
    ppa_srm_color_mode_t format = bpp == 2 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888;
    const ppa_srm_rotation_angle_t angles[] = {PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_270,
                                               PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_90
                                              };
    ppa_srm_oper_config_t op = {.in = {.buffer = request->source.pixels,
                                       .pic_w = request->source.layout.stride_bytes / bpp,
                                       .pic_h = request->source.layout.height,
                                       .block_w = request->source_area.width,
                                       .block_h = request->source_area.height,
                                       .block_offset_x = request->source_area.x,
                                       .block_offset_y = request->source_area.y,
                                       .srm_cm = format
                                      },
                                .out = {.buffer = request->destination.pixels,
                                        .buffer_size = request->destination.capacity_bytes,
                                        .pic_w = request->destination.layout.stride_bytes / bpp,
                                        .pic_h = request->destination.layout.height,
                                        .block_offset_x = request->destination_x,
                                        .block_offset_y = request->destination_y,
                                        .srm_cm = format
                                       },
                                .rotation_angle = angles[request->rotation],
                                .scale_x = 1,
                                .scale_y = 1,
                                .byte_swap = request->source.layout.byte_order != request->destination.layout.byte_order,
                                .mode = PPA_TRANS_MODE_BLOCKING
                               };
    error = ppa_do_scale_rotate_mirror(plugin->ppa, &op);
    if (error == ESP_OK) {
        error = hw_sync(band, band_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
done:
    present_mutex_give(plugin->gate);
    return error;
}
static esp_err_t ppa_destroy(void *context)
{
    plugin_t *plugin = context;
    esp_err_t error = ppa_unregister_client(plugin->ppa);
    if (error != ESP_OK) {
        return error;
    }
    present_mutex_destroy(plugin->gate);
    present_free(plugin);
    return ESP_OK;
}
static const esp_display_present_executor_ops_t ppa_ops = {ppa_caps, ppa_probe, ppa_execute, ppa_destroy};
esp_err_t esp_display_present_executor_ppa_create(esp_display_present_executor_t **out)
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
    esp_err_t error;
    ppa_client_config_t cfg = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    error = ppa_register_client(&cfg, &plugin->ppa);
    if (error != ESP_OK) {
        present_mutex_destroy(plugin->gate);
        present_free(plugin);
        return error;
    }
    plugin->base = (esp_display_present_executor_t) {
        &ppa_ops, plugin
    };
    *out = &plugin->base;
    return ESP_OK;
}
#else
esp_err_t esp_display_present_executor_ppa_create(esp_display_present_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
