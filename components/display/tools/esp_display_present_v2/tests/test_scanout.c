/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lcd_scanout.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_cache.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define OK(x) assert((x)==ESP_OK)
struct fake_lcd {
    void *buffers[3];
    dpi_cb_t dsi;
    rgb_cb_t rgb;
    void *user;
    bool early, fail, stop_timeout, deleted;
    size_t calls;
    const void *source;
};
static void boundary(struct fake_lcd *p)
{
    if (p->dsi) {
        (void)p->dsi(p, NULL, p->user);
    }
    if (p->rgb) {
        (void)p->rgb(p, NULL, p->user);
    }
}

esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_dpi_panel_event_callbacks_t *cb, void *user)
{
    p->dsi = cb->on_frame_buf_complete;
    p->user = user;
    assert(!cb->on_color_trans_done && !cb->on_vsync);
    return ESP_OK;
}

esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_callbacks_t *cb, void *user)
{
    p->rgb = cb->on_frame_buf_complete;
    p->user = user;
    assert(!cb->on_color_trans_done && !cb->on_vsync);
    return ESP_OK;
}

static esp_err_t get_buffers(esp_lcd_panel_handle_t p, uint32_t n, void **first, va_list args)
{
    *first = p->buffers[0];
    for (uint32_t i = 1; i < n; ++i) {
        *va_arg(args, void **) = p->buffers[i];
    }
    return ESP_OK;
}

esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **first, ...)
{
    va_list args;
    va_start(args, first);
    esp_err_t error = get_buffers(p, n, first, args);
    va_end(args);
    return error;
}

esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **first, ...)
{
    va_list args;
    va_start(args, first);
    esp_err_t error = get_buffers(p, n, first, args);
    va_end(args);
    return error;
}

esp_err_t esp_cache_msync(void *p, size_t bytes, int flags)
{
    assert(p && bytes == 12 && (flags & ESP_CACHE_MSYNC_FLAG_DIR_C2M));
    return ESP_OK;
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x, int y, int ex, int ey, const void *source)
{
    assert(x == 0 && y == 0 && ex == 3 && ey == 2);
    p->calls++;
    p->source = source;
    if (p->early) {
        boundary(p);
        boundary(p);
    }
    return p->fail ? ESP_FAIL : ESP_OK;
}

esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t p)
{
    if (p->stop_timeout) {
        return ESP_ERR_TIMEOUT;
    }
    p->dsi = NULL;
    p->rgb = NULL;
    p->deleted = true;
    return ESP_OK;
}

static void exercise(pv2_lcd_scanout_kind_t kind, bool fail)
{
    struct fake_lcd panel = {.early = true, .fail = fail};
    for (size_t i = 0; i < 2; ++i) {
        panel.buffers[i] = aligned_alloc(64, 64);
        assert(panel.buffers[i]);
    }
    pv2_lcd_scanout_config_t cfg = {.panel = &panel, .kind = kind, .width = 3, .height = 2,
                                    .format = PV2_FORMAT_RGB565, .buffer_count = 2, .rgb_bounce_buffer_mode = true, .delete_panel_on_stop = true
                                   };
    pv2_lcd_scanout_t *a;
    OK(pv2_lcd_scanout_create(&cfg, &a));
    void *buffers[3];
    size_t count;
    OK(pv2_lcd_scanout_get_buffers(a, buffers, 3, &count));
    assert(count == 2);
    pv2_panel_adapter_t interface;
    OK(pv2_lcd_scanout_get_interface(a, &interface));
    pv2_backend_config_t bc = {.width = 3, .height = 2, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
                               .format = PV2_FORMAT_RGB565, .render_contract = PV2_CONTRACT_FULL, .external_buffers = buffers,
                               .external_buffer_bytes = 64, .initial_scanout_buffer = buffers[0], .panel = interface
                              };
    const pv2_backend_ops_t *ops = &pv2_fb_backend;
    pv2_backend_t *b;
    OK(ops->create(&bc, &b));
    pv2_frame_t f;
    pv2_frame_request_t req = {.full_redraw = true};
    OK(ops->begin(b, &req, &f));
    pv2_lease_t lease;
    OK(ops->acquire(b, f, NULL, 0, &lease));
    assert(lease.surface.pixels == buffers[1]); /* FB0 is still scanning. */
    memset(lease.surface.pixels, 0x55, 12);
    OK(ops->submit_region(b, f, lease.id));
    pv2_receipt_t receipt;
    OK(ops->end(b, f, &receipt));
    OK(pv2_fb_service(b));
    pv2_frame_status_t status;
    OK(ops->query(b, receipt, &status));
    assert(!status.terminal);
    boundary(&panel);
    OK(ops->query(b, receipt, &status));
    assert(!status.terminal);
    boundary(&panel);
    OK(ops->query(b, receipt, &status));
    assert(status.terminal == !fail);
    if (!fail) {
        assert(status.present_confirmed);
        OK(ops->release_receipt(b, receipt));
    }
    panel.stop_timeout = true;
    assert(ops->stop(b, 0) == ESP_ERR_TIMEOUT);
    assert(pv2_lcd_scanout_destroy(a) == ESP_ERR_INVALID_STATE);
    panel.stop_timeout = false;
    OK(ops->stop(b, 0));
    if (fail) {
        OK(ops->query(b, receipt, &status));
        assert(status.terminal && status.result == ESP_FAIL);
        OK(ops->release_receipt(b, receipt));
    }
    OK(ops->destroy(b));
    OK(pv2_lcd_scanout_destroy(a));
    assert(panel.deleted);
    for (size_t i = 0; i < 2; ++i) {
        free(panel.buffers[i]);
    }
}
int main(void)
{
    exercise(PV2_LCD_DSI, false);
    exercise(PV2_LCD_DSI, true);
    exercise(PV2_LCD_RGB_BOUNCE, false);
    exercise(PV2_LCD_RGB_BOUNCE, true);
    puts("scanout: real adapter against driver stubs, boundary fence, initial scanout and stop retry passed");
}
