/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lcd_scanout.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_cache.h"
#include <assert.h>
#include <pthread.h>
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
    assert(!p->deleted); /* Must never double-delete panel on stop retry. */
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
    pv2_output_caps_t output_caps;
    OK(pv2_lcd_scanout_get_output_caps(a, &output_caps));
    assert(output_caps.full_frame_only && output_caps.external_buffers_required && output_caps.max_inflight == 1);
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
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool block, entered;
    pv2_panel_event_fn real_cb;
    void *real_user;
} cb_gate_t;

static bool blocking_panel_cb(void *user, const pv2_backend_event_t *event, bool isr)
{
    cb_gate_t *g = user;
    pthread_mutex_lock(&g->lock);
    g->entered = true;
    pthread_cond_broadcast(&g->changed);
    while (g->block) {
        pthread_cond_wait(&g->changed, &g->lock);
    }
    pthread_mutex_unlock(&g->lock);
    return g->real_cb ? g->real_cb(g->real_user, event, isr) : false;
}

static void *boundary_thread(void *arg)
{
    struct fake_lcd *p = arg;
    boundary(p);
    return NULL;
}

static void test_callback_drain_retry(void)
{
    struct fake_lcd panel = {0};
    for (size_t i = 0; i < 2; ++i) {
        panel.buffers[i] = aligned_alloc(64, 64);
        assert(panel.buffers[i]);
    }
    cb_gate_t gate = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER, .block = true};
    pv2_lcd_scanout_config_t cfg = {.panel = &panel, .kind = PV2_LCD_DSI, .width = 3, .height = 2,
                                    .format = PV2_FORMAT_RGB565, .buffer_count = 2, .delete_panel_on_stop = true
                                   };
    pv2_lcd_scanout_t *a;
    OK(pv2_lcd_scanout_create(&cfg, &a));
    void *buffers[2];
    size_t count;
    OK(pv2_lcd_scanout_get_buffers(a, buffers, 2, &count));
    pv2_panel_adapter_t interface;
    OK(pv2_lcd_scanout_get_interface(a, &interface));
    OK(interface.attach(interface.context, blocking_panel_cb, &gate));
    pv2_surface_t surface = {buffers[1], 64, {3, 2, 6, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}};
    pv2_rect_t area = {0, 0, 3, 2};
    pv2_tx_result_t tx = interface.submit(interface.context, &surface, &area, 42);
    assert(tx.acceptance == PV2_TX_ACCEPTED && tx.result == ESP_OK);
    boundary(&panel);
    pthread_t th;
    assert(pthread_create(&th, NULL, boundary_thread, &panel) == 0);
    pthread_mutex_lock(&gate.lock);
    while (!gate.entered) {
        pthread_cond_wait(&gate.changed, &gate.lock);
    }
    pthread_mutex_unlock(&gate.lock);
    /* Panel deletes once, then callback drain times out; retry must not call esp_lcd_panel_del again. */
    assert(interface.quiesce(interface.context, 0) == ESP_ERR_TIMEOUT);
    assert(panel.deleted);
    assert(interface.quiesce(interface.context, 0) == ESP_ERR_TIMEOUT);
    pthread_mutex_lock(&gate.lock);
    gate.block = false;
    pthread_cond_broadcast(&gate.changed);
    pthread_mutex_unlock(&gate.lock);
    assert(pthread_join(th, NULL) == 0);
    OK(interface.quiesce(interface.context, UINT32_MAX));
    OK(pv2_lcd_scanout_destroy(a));
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.lock);
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
    test_callback_drain_retry();
    puts("scanout: real adapter against driver stubs, boundary fence, initial scanout and stop retry passed");
}
