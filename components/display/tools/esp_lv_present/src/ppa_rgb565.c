/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ppa_rgb565.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <stdatomic.h>
#include <stdint.h>

/* Lifecycle and lease publication run on the LVGL owner. Two software draw
 * workers can read the lease and execute concurrently on disjoint rows. */
static ppa_client_handle_t client;
static _Atomic(uintptr_t) active_lease;
static atomic_int fill_error;

bool present_lvgl_ppa_fill_start(void)
{
    if (client) {
        return false;
    }
    ppa_client_config_t config = {
        .oper_type = PPA_OPERATION_FILL,
        .max_pending_trans_num = 2,
    };
    ppa_client_handle_t handle = NULL;
    if (ppa_register_client(&config, &handle) != ESP_OK) {
        return false;
    }
    client = handle;
    atomic_store_explicit(&fill_error, ESP_OK, memory_order_relaxed);
    return true;
}

esp_err_t present_lvgl_ppa_fill_stop(void)
{
    esp_err_t error = ppa_unregister_client(client);
    if (error == ESP_OK) {
        client = NULL;
    }
    return error;
}

void present_lvgl_ppa_fill_set_lease(void *pixels, size_t bytes)
{
    uintptr_t base = (uintptr_t)pixels;
    if (pixels && bytes == PRESENT_LVGL_PPA_FRAME_BYTES && esp_ptr_external_ram(pixels) && base % 64 == 0) {
        size_t alignment = esp_cache_get_line_size_by_addr(pixels);
        if (alignment && 64 % alignment == 0) {
            atomic_store_explicit(&fill_error, ESP_OK, memory_order_relaxed);
            atomic_store_explicit(&active_lease, base, memory_order_release);
            return;
        }
    }
    atomic_store_explicit(&active_lease, 0, memory_order_release);
}

esp_err_t present_lvgl_ppa_fill_error(void)
{
    return atomic_load_explicit(&fill_error, memory_order_acquire);
}

void __real_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *draw);
void __wrap_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *draw)
{
    uintptr_t base = atomic_load_explicit(&active_lease, memory_order_acquire);
    uintptr_t destination = (uintptr_t)draw->dest_buf;
    if (!base || destination < base || draw->mask_buf || draw->opa < LV_OPA_MAX ||
            draw->dest_w != PRESENT_LVGL_PPA_WIDTH || draw->dest_h < 96 || draw->dest_h > PRESENT_LVGL_PPA_HEIGHT ||
            draw->dest_stride != PRESENT_LVGL_PPA_STRIDE) {
        __real_lv_draw_sw_blend_color_to_rgb565(draw);
        return;
    }
    size_t offset = destination - base;
    size_t bytes = (size_t)draw->dest_h * PRESENT_LVGL_PPA_STRIDE;
    if (offset % PRESENT_LVGL_PPA_STRIDE || offset > PRESENT_LVGL_PPA_FRAME_BYTES - bytes) {
        __real_lv_draw_sw_blend_color_to_rgb565(draw);
        return;
    }

    /* Both ends are cache-line aligned. The driver writes back and invalidates
     * the window before starting PPA. Invalidate again before the next draw. */
    ppa_fill_oper_config_t config = {
        .out = {
            .buffer = draw->dest_buf, .buffer_size = bytes,
            .pic_w = draw->dest_w, .pic_h = draw->dest_h, .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
        },
        .fill_block_w = draw->dest_w, .fill_block_h = draw->dest_h,
        .fill_argb_color = {.a = 255, .r = draw->color.red, .g = draw->color.green, .b = draw->color.blue},
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t error = ppa_do_fill(client, &config);
    if (error == ESP_OK) {
        error = esp_cache_msync(draw->dest_buf, bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    if (error != ESP_OK) {
        atomic_store_explicit(&fill_error, error, memory_order_release);
    }
}
