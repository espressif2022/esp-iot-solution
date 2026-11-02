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
/* Model an ISR that fetched callback/user before unregister but had not
 * entered the adapter. active_callbacks cannot see it; the native join must. */
static dpi_cb_t late_dsi;
static rgb_cb_t late_rgb;
static void *late_user;
static esp_lcd_panel_handle_t late_panel;
static bool late_joined;
esp_err_t __real_pv2_isr_barrier(uint32_t timeout);
esp_err_t __wrap_pv2_isr_barrier(uint32_t timeout)
{
    if (late_dsi) {
        (void)late_dsi(late_panel, NULL, late_user);
        late_dsi = NULL;
        late_joined = true;
    }
    if (late_rgb) {
        (void)late_rgb(late_panel, NULL, late_user);
        late_rgb = NULL;
        late_joined = true;
    }
    return __real_pv2_isr_barrier(timeout);
}
struct fake_lcd {
    void *buffers[3];
    dpi_cb_t dsi, dsi_vsync;
    rgb_cb_t rgb, rgb_vsync;
    void *user;
    bool early, fail, stop_timeout, deleted, detach_fail;
    size_t calls;
    bool no_bounce, one_shot, transformed, custom_bounce, one_fb;
    const void *source;
};
esp_err_t esp_lcd_rgb_panel_get_frame_buffer_info(esp_lcd_panel_handle_t panel, esp_lcd_rgb_panel_frame_buffer_info_t *info)
{
    *info = (esp_lcd_rgb_panel_frame_buffer_info_t) {
        .width = 3, .height = 2,
        .frame_buffer_count = panel->one_fb ? 1 : 2, .frame_buffer_bytes = 12, .bits_per_pixel = 16,
        .bounce_buffer_bytes = panel->no_bounce ? 0 : 6, .continuous_refresh = !panel->one_shot,
        .frame_buffer_retirement = !panel->no_bounce, .transformed = panel->transformed,
        .custom_bounce_callback = panel->custom_bounce
    };
    return ESP_OK;
}
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
    if (!user && p->detach_fail) {
        return ESP_FAIL;
    }
    if (!user && p->dsi) {
        (void)p->dsi(p, NULL, NULL); /* Old callback, newly cleared user_ctx. */
        if (p->dsi_vsync) {
            (void)p->dsi_vsync(p, NULL, NULL);
        }
    }
    p->dsi = cb->on_frame_buf_complete;
    p->dsi_vsync = cb->on_vsync;
    p->user = user;
    assert(!cb->on_color_trans_done);
    return ESP_OK;
}

esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_callbacks_t *cb, void *user)
{
    if (!user && p->detach_fail) {
        return ESP_FAIL;
    }
    if (!user && p->rgb) {
        (void)p->rgb(p, NULL, NULL);
        if (p->rgb_vsync) {
            (void)p->rgb_vsync(p, NULL, NULL);
        }
    }
    p->rgb = cb->on_frame_buf_complete;
    p->rgb_vsync = cb->on_vsync;
    p->user = user;
    assert(!cb->on_color_trans_done);
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

static bool raw_vsync(void *user, uint64_t timestamp, bool isr)
{
    assert(isr && timestamp);
    ++*(unsigned *)user;
    return false;
}
static void test_retained_scanout(void)
{
    for (unsigned rgb = 0; rgb < 2; ++rgb) {
        struct fake_lcd panel = {0};
        for (unsigned i = 0; i < 2; ++i) {
            panel.buffers[i] = aligned_alloc(64, 64);
            assert(panel.buffers[i]);
        }
        unsigned vsyncs = 0;
        pv2_lcd_scanout_config_t config = {
            .panel = &panel, .kind = rgb ? PV2_LCD_RGB_BOUNCE : PV2_LCD_DSI,
            .width = 3, .height = 2, .format = PV2_FORMAT_RGB565, .buffer_count = 2,
            .rgb_bounce_buffer_mode = true, .retain_driver_scanout = true,
            .on_vsync = raw_vsync, .vsync_user = &vsyncs,
        };
        pv2_lcd_scanout_t *adapter;
        config.delete_panel_on_stop = true;
        assert(pv2_lcd_scanout_create(&config, &adapter) == ESP_ERR_INVALID_ARG);
        config.delete_panel_on_stop = false;
        OK(pv2_lcd_scanout_create(&config, &adapter));
        pv2_panel_adapter_t interface;
        OK(pv2_lcd_scanout_get_interface(adapter, &interface));
        cb_gate_t gate = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
        OK(interface.attach(interface.context, blocking_panel_cb, &gate));
        if (rgb) {
            (void)panel.rgb_vsync(&panel, NULL, panel.user);
        } else {
            (void)panel.dsi_vsync(&panel, NULL, panel.user);
        }
        assert(vsyncs == 1 && !gate.entered); /* Raw VSYNC is never a retire fence. */
        pv2_surface_t surface = {panel.buffers[1], 64, {3, 2, 6, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}};
        pv2_rect_t area = {0, 0, 3, 2};
        assert(interface.submit(interface.context, &surface, &area, 42).acceptance == PV2_TX_ACCEPTED);
        assert(interface.quiesce(interface.context, 0) == ESP_ERR_TIMEOUT);
        assert(pv2_lcd_scanout_destroy(adapter) == ESP_ERR_INVALID_STATE);
        boundary(&panel);
        assert(!gate.entered);
        boundary(&panel); /* Completion still drains while retained adapter closes. */
        assert(gate.entered);
        panel.detach_fail = true;
        assert(interface.quiesce(interface.context, 0) == ESP_FAIL);
        panel.detach_fail = false;
        late_dsi = panel.dsi;
        late_rgb = panel.rgb;
        late_user = panel.user;
        late_panel = &panel;
        late_joined = false;
        OK(interface.quiesce(interface.context, 0));
        assert(late_joined); /* Adapter was still alive when the cached ISR ran. */
        assert(!panel.deleted && !panel.rgb && !panel.dsi && panel.source == panel.buffers[1]);
        OK(pv2_lcd_scanout_destroy(adapter));
        pthread_cond_destroy(&gate.changed);
        pthread_mutex_destroy(&gate.lock);
        for (unsigned i = 0; i < 2; ++i) {
            free(panel.buffers[i]);
        }
    }
}

static bool count_single(void *user, const pv2_backend_event_t *event, bool isr)
{
    assert(isr && event->kind == PV2_EVENT_SCANOUT_CONFIRMED && event->transaction_cookie == 91);
    ++*(unsigned *)user;
    return false;
}
static void queried_rgb(void)
{
    struct fake_lcd panel = {0};
    panel.buffers[0] = aligned_alloc(64, 64);
    panel.buffers[1] = aligned_alloc(64, 64);
    pv2_lcd_scanout_config_t config = {.panel = &panel, .kind = PV2_LCD_RGB_BOUNCE,
                                       .width = 3, .height = 2, .format = PV2_FORMAT_RGB565, .buffer_count = 2, .retain_driver_scanout = true
                                      };
    pv2_lcd_scanout_t *adapter;
    /* No confirmation switch: the driver is authoritative. */
    OK(pv2_lcd_scanout_create(&config, &adapter));
    OK(pv2_lcd_scanout_destroy(adapter));
    panel.one_shot = true;
    assert(pv2_lcd_scanout_create(&config, &adapter) == ESP_ERR_NOT_SUPPORTED);
    panel.one_shot = false;
    panel.custom_bounce = true;
    assert(pv2_lcd_scanout_create(&config, &adapter) == ESP_ERR_NOT_SUPPORTED);
    panel.custom_bounce = false;
    panel.transformed = true;
    assert(pv2_lcd_scanout_create(&config, &adapter) == ESP_ERR_NOT_SUPPORTED);
    panel.transformed = false;
    panel.no_bounce = true;
    config.rgb_bounce_buffer_mode = true; /* A stale caller hint cannot override facts. */
    assert(pv2_lcd_scanout_create(&config, &adapter) == ESP_ERR_NOT_SUPPORTED);
    panel.one_fb = true;
    config.buffer_count = 1;
    OK(pv2_lcd_scanout_create(&config, &adapter));
    pv2_panel_adapter_t api;
    OK(pv2_lcd_scanout_get_interface(adapter, &api));
    unsigned confirmed = 0;
    OK(api.attach(api.context, count_single, &confirmed));
    pv2_surface_t surface = {panel.buffers[0], 12, {3, 2, 6, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}};
    pv2_rect_t area = {0, 0, 3, 2};
    assert(api.submit(api.context, &surface, &area, 91).acceptance == PV2_TX_ACCEPTED);
    assert(!panel.rgb && panel.rgb_vsync);
    (void)panel.rgb_vsync(&panel, NULL, panel.user);
    assert(!confirmed);
    (void)panel.rgb_vsync(&panel, NULL, panel.user);
    assert(confirmed == 1);
    OK(api.quiesce(api.context, 100));
    OK(pv2_lcd_scanout_destroy(adapter));
    free(panel.buffers[0]);
    free(panel.buffers[1]);
}
int main(void)
{
    /* Direct RGB is deliberately outside the audited bounce adapter contract. */
    struct fake_lcd unsupported_panel = {.no_bounce = true};
    pv2_lcd_scanout_config_t unsupported = {
        .panel = &unsupported_panel, .kind = PV2_LCD_RGB_BOUNCE,
        .width = 3, .height = 2, .format = PV2_FORMAT_RGB565,
        .buffer_count = 2, .delete_panel_on_stop = true,
    };
    pv2_lcd_scanout_t *unsupported_adapter = NULL;
    assert(pv2_lcd_scanout_create(&unsupported, &unsupported_adapter) == ESP_ERR_NOT_SUPPORTED);
    assert(!unsupported_adapter && !unsupported_panel.deleted);
    queried_rgb();
    exercise(PV2_LCD_DSI, false);
    exercise(PV2_LCD_DSI, true);
    exercise(PV2_LCD_RGB_BOUNCE, false);
    exercise(PV2_LCD_RGB_BOUNCE, true);
    test_callback_drain_retry();
    test_retained_scanout();
    puts("scanout: real adapter against driver stubs, boundary fence, initial scanout and stop retry passed");
}
