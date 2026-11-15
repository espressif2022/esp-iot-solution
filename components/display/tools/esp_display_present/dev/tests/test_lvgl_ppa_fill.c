/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ppa_rgb565.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"
#include <assert.h>

static _Alignas(64) uint8_t lease[PRESENT_LVGL_PPA_FRAME_BYTES];
static unsigned cpu_calls, ppa_calls, cache_calls;
static esp_err_t fill_result = ESP_OK, cache_result = ESP_OK;
static void *last_window;
static size_t last_window_bytes, cache_align = 64;

bool esp_ptr_external_ram(const void *pointer)
{
    return pointer == lease;
}

size_t esp_cache_get_line_size_by_addr(const void *pointer)
{
    assert(pointer == lease);
    return cache_align;
}

esp_err_t ppa_register_client(const ppa_client_config_t *config, ppa_client_handle_t *result)
{
    assert(config->oper_type == PPA_OPERATION_FILL && config->max_pending_trans_num == 2);
    *result = lease;
    return ESP_OK;
}

esp_err_t ppa_unregister_client(ppa_client_handle_t handle)
{
    assert(handle == lease);
    return ESP_OK;
}

esp_err_t ppa_do_fill(ppa_client_handle_t handle, const ppa_fill_oper_config_t *fill)
{
    assert(handle == lease && fill->mode == PPA_TRANS_MODE_BLOCKING);
    assert(fill->fill_block_w == PRESENT_LVGL_PPA_WIDTH && fill->out.pic_w == PRESENT_LVGL_PPA_WIDTH);
    assert(fill->out.pic_h == fill->fill_block_h && fill->out.buffer_size == PRESENT_LVGL_PPA_STRIDE * fill->out.pic_h);
    assert(fill->fill_argb_color.a == LV_OPA_COVER && fill->fill_argb_color.r == 0x12 &&
           fill->fill_argb_color.g == 0x34 && fill->fill_argb_color.b == 0x56);
    last_window = fill->out.buffer;
    last_window_bytes = fill->out.buffer_size;
    ++ppa_calls;
    return fill_result;
}

esp_err_t esp_cache_msync(void *address, size_t bytes, int flags)
{
    assert(address == last_window && bytes == last_window_bytes && flags == ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    ++cache_calls;
    return cache_result;
}

void __real_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *draw)
{
    assert(draw);
    ++cpu_calls;
}

void __wrap_lv_draw_sw_blend_color_to_rgb565(lv_draw_sw_blend_fill_dsc_t *draw);

int main(void)
{
    enum { HALF_HEIGHT = PRESENT_LVGL_PPA_HEIGHT / 2 };
    lv_draw_sw_blend_fill_dsc_t draw = {
        .dest_buf = lease + HALF_HEIGHT * PRESENT_LVGL_PPA_STRIDE,
        .dest_w = PRESENT_LVGL_PPA_WIDTH, .dest_h = HALF_HEIGHT, .dest_stride = PRESENT_LVGL_PPA_STRIDE,
        .opa = LV_OPA_COVER, .color = lv_color_hex(0x123456),
    };
    assert(present_lvgl_ppa_fill_start());
    assert(!present_lvgl_ppa_fill_start());
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(cpu_calls == 1 && !ppa_calls);

    present_lvgl_ppa_fill_set_lease(lease, sizeof(lease));
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(cpu_calls == 1 && ppa_calls == 1 && cache_calls == 1 && last_window == draw.dest_buf);

    draw.dest_h = HALF_HEIGHT + 1; /* The last row would be outside the lease. */
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    draw.dest_h = HALF_HEIGHT;
    draw.mask_buf = lease;
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    draw.mask_buf = NULL;
    cache_align = 128; /* Exercise the fallback for unverified cache geometry. */
    present_lvgl_ppa_fill_set_lease(lease, sizeof(lease));
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(cpu_calls == 4 && ppa_calls == 1 && cache_calls == 1);

    cache_align = 64;
    present_lvgl_ppa_fill_set_lease(lease, sizeof(lease));
    fill_result = ESP_FAIL;
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(present_lvgl_ppa_fill_error() == ESP_FAIL && cpu_calls == 4 && ppa_calls == 2 && cache_calls == 1);
    present_lvgl_ppa_fill_set_lease(NULL, 0);
    assert(present_lvgl_ppa_fill_error() == ESP_FAIL);
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(cpu_calls == 5 && ppa_calls == 2);

    present_lvgl_ppa_fill_set_lease(lease, sizeof(lease));
    assert(present_lvgl_ppa_fill_error() == ESP_OK);
    fill_result = ESP_OK;
    cache_result = ESP_ERR_INVALID_STATE;
    __wrap_lv_draw_sw_blend_color_to_rgb565(&draw);
    assert(present_lvgl_ppa_fill_error() == ESP_ERR_INVALID_STATE && cpu_calls == 5 && ppa_calls == 3 && cache_calls == 2);
    present_lvgl_ppa_fill_set_lease(NULL, 0);
    assert(present_lvgl_ppa_fill_stop() == ESP_OK);
}
