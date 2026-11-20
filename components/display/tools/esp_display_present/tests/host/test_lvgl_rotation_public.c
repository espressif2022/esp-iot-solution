/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#define esp_lcd_panel_draw_bitmap fixture_draw
#include "support/legacy_panel.h"
#undef esp_lcd_panel_draw_bitmap
#include "esp_lv_present.h"
#if LVGL_VERSION_MINOR >= 5
#include "lvgl_private.h"
#elif LVGL_VERSION_MINOR == 0
#include "src/display/lv_display_private.h"
#define lv_display_get_buf_active(display) ((display)->buf_act)
#endif
#include <stdatomic.h>
#include <unistd.h>
static bool io_mode, hold_io, fail_draw, fail_borrow;
static uint16_t pixels[32];
static atomic_bool scanning;
static esp_display_presenter_t *presenter;
static unsigned events, draw_allocations, held_completions;
lv_draw_buf_t *__real_lv_draw_buf_create(uint32_t, uint32_t, lv_color_format_t, uint32_t);
lv_draw_buf_t *__wrap_lv_draw_buf_create(uint32_t w, uint32_t h, lv_color_format_t f, uint32_t s)
{
    ++draw_allocations;
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
    if (io_mode) {
        /* The tile layout is unchanged by 180 degrees, and by quarter turns on
         * this square fixture. Neither transition needs replacement storage. */
        lv_draw_buf_t *tile = lv_display_get_buf_active(d);
        unsigned allocations = draw_allocations;
        OK(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_180, 1000));
        assert(draw_allocations == allocations && lv_display_get_buf_active(d) == tile);
        if (width == 4) {
            OK(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000));
            assert(draw_allocations == allocations && lv_display_get_buf_active(d) == tile);
        }
        OK(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_0, 1000));
        assert(draw_allocations == allocations && lv_display_get_buf_active(d) == tile);
    }
    esp_display_presenter_frame_status_t before, after;
    OK(esp_display_presenter_get_frame_status(presenter, &before));
    if (mode != ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL && (width != 4 || (LVGL_VERSION_MINOR >= 5 && mode == ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT))) {
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
        /* Faster canvas reuse can queue both tiles before the timeout. Release
         * every accepted transfer, rather than dropping the second completion. */
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
#if LVGL_VERSION_MINOR >= 5
/* Keep creation fallback distinct from a borrowed DIRECT temporarily rotated
 * through COPY. Returning to 0 degrees must preserve the original policy. */
static void direct_path_policy(bool start_rotated)
{
    struct fake_lcd p;
    init_panel(&p);
    io_mode = false;
    esp_display_presenter_config_t cfg = config(&p, false);
    cfg.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT;
    if (start_rotated) {
        cfg.width = 4;
        cfg.height = 8;
        cfg.target.hw.rotation = ESP_DISPLAY_PRESENT_ROTATE_90;
    }
    OK(esp_display_presenter_create(&cfg, &presenter));
    pthread_t scanner;
    atomic_store(&scanning, true);
    assert(!pthread_create(&scanner, NULL, scan, &p));
    lv_display_t *display;
    OK(esp_lv_present_start(presenter, &display));
    lv_obj_t *screen = lv_display_get_screen_active(display);
    assert(lv_display_is_double_buffered(display) == !start_rotated);
    lv_refr_now(display);
    OK(esp_display_presenter_quiesce(presenter, 1000));
    for (unsigned i = 0; i < 2; ++i) {
        esp_display_present_rotation_t angle = (i == 0) == start_rotated ?
                                               ESP_DISPLAY_PRESENT_ROTATE_0 : ESP_DISPLAY_PRESENT_ROTATE_90;
        OK(esp_lv_present_set_rotation(angle, 1000));
        assert(lv_display_get_screen_active(display) == screen);
        assert(lv_display_is_double_buffered(display) == (!start_rotated && angle == ESP_DISPLAY_PRESENT_ROTATE_0));
        lv_refr_now(display);
        OK(esp_display_presenter_quiesce(presenter, 1000));
        assert((lv_display_get_buf_active(display)->data == NULL) ==
               (!start_rotated && angle == ESP_DISPLAY_PRESENT_ROTATE_0));
    }
    OK(esp_lv_present_stop());
    OK(esp_display_presenter_quiesce(presenter, 1000));
    atomic_store(&scanning, false);
    assert(!pthread_join(scanner, NULL));
    OK(esp_display_presenter_delete(presenter));
    free_panel(&p);
    lv_deinit();
}

static uint32_t aligned_stride(uint32_t width, lv_color_format_t format)
{
    uint32_t bytes = (width * lv_color_format_get_bpp(format) + 7) / 8;
    return (bytes + 3) & ~3u;
}

/* Characterize the existing rotation admission limit, independently of the
 * planned structural refactor. A rejected rotation must leave the UI usable. */
static void direct_stride_rejection(void)
{
    lv_init();
    lv_draw_buf_handlers_t *handlers = lv_draw_buf_get_handlers();
    lv_draw_buf_width_to_stride_cb_t previous = handlers->width_to_stride_cb;
    handlers->width_to_stride_cb = aligned_stride;
    struct fake_lcd p;
    init_panel(&p);
    p.physical_height = 3;
    io_mode = false;
    esp_display_presenter_config_t cfg = config(&p, true);
    cfg.height = 3;
    cfg.target.drawbuf.lines = 2;
    cfg.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT;
    OK(esp_display_presenter_create(&cfg, &presenter));
    pthread_t scanner;
    atomic_store(&scanning, true);
    assert(!pthread_create(&scanner, NULL, scan, &p));
    lv_display_t *display;
    OK(esp_lv_present_start(presenter, &display));
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_refr_now(display);
    OK(esp_display_presenter_quiesce(presenter, 1000));
    esp_display_presenter_frame_status_t before, after;
    OK(esp_display_presenter_get_frame_status(presenter, &before));
    bool paused = lv_timer_get_paused(lv_display_get_refr_timer(display));
    assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000) == ESP_ERR_NOT_SUPPORTED);
    OK(esp_display_presenter_get_frame_status(presenter, &after));
    assert(before.generation == after.generation);
    assert(lv_display_get_horizontal_resolution(display) == 8 && lv_display_get_vertical_resolution(display) == 3);
    assert(lv_display_is_double_buffered(display));
    assert(lv_timer_get_paused(lv_display_get_refr_timer(display)) == paused);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0000ff), 0);
    lv_refr_now(display);
    OK(esp_display_presenter_quiesce(presenter, 1000));
    for (unsigned i = 0; i < 8 * 3; ++i) {
        assert(((const uint16_t *)p.source)[i] == 0x001f);
    }
    OK(esp_lv_present_stop());
    OK(esp_display_presenter_quiesce(presenter, 1000));
    atomic_store(&scanning, false);
    assert(!pthread_join(scanner, NULL));
    OK(esp_display_presenter_delete(presenter));
    free_panel(&p);
    handlers->width_to_stride_cb = previous;
    lv_deinit();
}
#endif

int main(void)
{
    assert(esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_0, 0) == ESP_ERR_INVALID_STATE);
    const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT};
    for (unsigned m = 0; m < 3; ++m) for (unsigned w = 4; w <= 8; w += 4) {
            exercise(modes[m], w);
        }
#if LVGL_VERSION_MINOR >= 5
    direct_path_policy(false);
    direct_path_policy(true);
    direct_stride_rejection();
#endif
    assert(events);
    puts("public LVGL rotation: pixels, facade geometry, ownership, OOM, timeout, late failure passed");
}
