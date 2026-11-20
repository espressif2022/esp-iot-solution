/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#define esp_lcd_panel_draw_bitmap fixture_draw
#include "support/legacy_panel.h"
#undef esp_lcd_panel_draw_bitmap
#include "esp_lv_present.h"
#include <stdatomic.h>
#include <unistd.h>
static bool io_mode, hold_io, fail_draw, fail_borrow;
static uint16_t pixels[32];
static atomic_bool scanning;
static esp_display_presenter_t *presenter;
static unsigned events, held_completions;
lv_draw_buf_t *__real_lv_draw_buf_create(uint32_t, uint32_t, lv_color_format_t, uint32_t);
lv_draw_buf_t *__wrap_lv_draw_buf_create(uint32_t w, uint32_t h, lv_color_format_t f, uint32_t s)
{
    if (fail_draw) {
        fail_draw = false;
        return NULL;
    }
    return __real_lv_draw_buf_create(w, h, f, s);
}
esp_err_t __real_esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *, esp_display_present_lease_t *);
esp_err_t __wrap_esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *p, esp_display_present_lease_t *l)
{
    if (fail_borrow) {
        fail_borrow = false;
        return ESP_ERR_NO_MEM;
    }
    return __real_esp_display_present_engine_acquire_buffer(p, l);
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x, int y, int ex, int ey, const void *src)
{
    if (!io_mode) {
        return fixture_draw(p, x, y, ex, ey, src);
    }
    for (int row = y; row < ey; ++row) {
        memcpy(pixels + row * p->physical_width + x, (const uint16_t *)src + (row - y) * (ex - x), (ex - x) * 2);
    }
    pthread_mutex_lock(&p->lock);
    bool complete = !hold_io;
    if (!complete) {
        ++held_completions;
    }
    pthread_mutex_unlock(&p->lock);
    if (complete && p->io) {
        p->io(p, NULL, p->user);
    }
    return ESP_OK;
}
static void *scan(void *arg)
{
    while (atomic_load(&scanning)) {
        boundary(arg);
        usleep(1000);
    }
    return NULL;
}
static void *wrong_task(void *arg)
{
    (void)arg;
    assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 10) == ESP_ERR_INVALID_STATE);
    return NULL;
}
static void reject_refresh_rotation(lv_event_t *e)
{
    (void)e;
    assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 0) == ESP_ERR_INVALID_STATE);
}
static void resolution(lv_event_t *e)
{
    esp_display_presenter_caps_t caps;
    OK(esp_display_presenter_get_caps(presenter, &caps));
    lv_display_t *d = lv_event_get_target(e);
    assert(caps.width == lv_display_get_horizontal_resolution(d));
    assert(caps.height == lv_display_get_vertical_resolution(d));
    ++events;
}
static void exercise(esp_display_present_mode_t mode, unsigned width)
{
    struct fake_lcd p;
    init_panel(&p); p.physical_width = width; p.physical_height = 4;
    io_mode = mode == ESP_DISPLAY_PRESENT_MODE_NONE;
    esp_display_presenter_config_t cfg = config(&p, false);
    cfg.width = width; cfg.target.fb.mode = mode;
    cfg.target.drawbuf.lines = 2; cfg.target.drawbuf.buffers = 2;
    if (io_mode) {
        cfg.target.hw.io = &p;
        cfg.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    }
    cfg.transfer_timeout_ms = 1000;
    OK(esp_display_presenter_create(&cfg, &presenter));
    pthread_t scanner;
    atomic_store(&scanning, true);
    assert(!pthread_create(&scanner, NULL, scan, &p));
    lv_display_t *d;
    OK(esp_lv_present_start(presenter, &d));
    lv_display_add_event_cb(d, resolution, LV_EVENT_RESOLUTION_CHANGED, NULL);
    lv_display_add_event_cb(d, reject_refresh_rotation, LV_EVENT_RENDER_START, NULL);
    lv_obj_t *screen = lv_display_get_screen_active(d);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_remove_style_all(box); lv_obj_set_pos(box, 1, 1); lv_obj_set_size(box, 2, 2);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xff0000), 0); lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_refr_now(d); OK(esp_display_presenter_quiesce(presenter, 1000));
    pthread_t thread; assert(!pthread_create(&thread, NULL, wrong_task, NULL)); assert(!pthread_join(thread, NULL));
    assert(esp_lv_present_set_rotation((esp_display_present_rotation_t)45, 10) == ESP_ERR_INVALID_ARG);
    assert(esp_display_presenter_set_rotation(presenter, ESP_DISPLAY_PRESENT_ROTATE_90) == ESP_ERR_INVALID_STATE);
    esp_display_presenter_frame_status_t before, after;
    OK(esp_display_presenter_get_frame_status(presenter, &before));
    if (mode != ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL) {
        fail_draw = true;
        assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000) == ESP_ERR_NO_MEM);
        assert(!fail_draw);
    }
    OK(esp_display_presenter_get_frame_status(presenter, &after)); assert(before.generation == after.generation);
    for (unsigned i = 1; i <= 8; ++i) {
        unsigned angle = (i % 4) * 90;
        OK(esp_lv_present_set_rotation((esp_display_present_rotation_t)angle, 1000));
        OK(esp_display_presenter_get_frame_status(presenter, &after)); assert(after.generation == before.generation + i);
        OK(esp_lv_present_set_rotation((esp_display_present_rotation_t)angle, 1000));
        OK(esp_display_presenter_get_frame_status(presenter, &after)); assert(after.generation == before.generation + i);
        assert(lv_display_get_screen_active(d) == screen && lv_obj_is_valid(box));
        esp_display_presenter_effective_config_t effective;
        OK(esp_display_presenter_get_effective_config(presenter, &effective)); assert(effective.rotation == angle);
        lv_refr_now(d); OK(esp_display_presenter_quiesce(presenter, 1000));
        const uint16_t *seen = io_mode ? pixels : p.source;
        for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < width; ++x) {
                int32_t lx = x, ly = y;
                OK(esp_display_presenter_map_point(presenter, &lx, &ly));
                int ex = x, ey = y;
                if (angle == 90) {
                    ex = y;
                    ey = width - 1 - x;
                }
                if (angle == 180) {
                    ex = width - 1 - x;
                    ey = 3 - y;
                }
                if (angle == 270) {
                    ex = 3 - y;
                    ey = x;
                }
                assert(lx == ex && ly == ey);
                assert(seen[y * width + x] == (ex >= 1 && ex < 3 && ey >= 1 && ey < 3 ? 0xf800 : 0xffff));
            }
    }
    if (io_mode) {
        pthread_mutex_lock(&p.lock);
        hold_io = true;
        pthread_mutex_unlock(&p.lock);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x0000ff), 0); lv_refr_now(d);
        assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 0) == ESP_ERR_TIMEOUT);
        OK(esp_display_presenter_get_frame_status(presenter, &after)); assert(after.generation == before.generation + 8);
        /* Deliver each accepted transfer exactly once. Both input tiles may
         * already be queued when the zero-deadline rotation attempt returns. */
        pthread_mutex_lock(&p.lock);
        hold_io = false;
        unsigned completions = held_completions;
        held_completions = 0;
        pthread_mutex_unlock(&p.lock);
        while (completions--) {
            p.io(&p, NULL, p.user);
        }
        OK(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000));
        lv_refr_now(d); OK(esp_display_presenter_quiesce(presenter, 1000));
    }
#if LVGL_VERSION_MINOR >= 5
    if (!io_mode) {
        OK(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000));
        fail_borrow = true;
        assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_0, 1000) == ESP_ERR_NO_MEM);
        assert(!fail_borrow);
        esp_display_presenter_effective_config_t effective;
        OK(esp_display_presenter_get_effective_config(presenter, &effective)); assert(effective.rotation == 0);
        assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000) == ESP_ERR_INVALID_STATE);
        unsigned frames = esp_lv_present_get_frame_count(); lv_refr_now(d);
        assert(esp_lv_present_get_frame_count() == frames);
    }
#endif
    OK(esp_lv_present_stop());
    OK(esp_display_presenter_quiesce(presenter, 1000));
    atomic_store(&scanning, false); assert(!pthread_join(scanner, NULL));
    OK(esp_display_presenter_delete(presenter));
    free_panel(&p); lv_deinit();
}
int main(void)
{
    assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_0, 0) == ESP_ERR_INVALID_STATE);
    const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT};
    for (unsigned m = 0; m < 3; ++m) for (unsigned w = 4; w <= 8; w += 4) {
            exercise(modes[m], w);
        }
    assert(events);
    puts("public LVGL rotation: pixels, facade geometry, ownership, OOM, timeout, late failure passed");
}
