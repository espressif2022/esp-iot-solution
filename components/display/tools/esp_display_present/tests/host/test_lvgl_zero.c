/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/present_engine.h"
#include "core/present_control.h"
#include "present_lvgl.h"
#include "pixel/present_pixel_ops.h"
#if LVGL_VERSION_MINOR == 0
#include "src/display/lv_display_private.h"
#define lv_display_get_buf_active(display) ((display)->buf_act)
#endif
#include "scan_panel.h"
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include "src/display/lv_display_private.h"
#define OK(x) assert((x)==ESP_OK)
static unsigned canvas_allocations;
lv_draw_buf_t *__real_lv_draw_buf_create(uint32_t width, uint32_t height, lv_color_format_t format, uint32_t stride);
lv_draw_buf_t *__wrap_lv_draw_buf_create(uint32_t width, uint32_t height, lv_color_format_t format, uint32_t stride)
{
    ++canvas_allocations;
    return __real_lv_draw_buf_create(width, height, format, stride);
}
static bool fail_acquire;
static unsigned blocking_acquires;
static lv_draw_buf_t *watched_draw;
static unsigned watched_destroyed;
esp_err_t __real_esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *, esp_display_present_lease_t *);
esp_err_t __wrap_esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *engine, esp_display_present_lease_t *lease)
{
    ++blocking_acquires;
    if (fail_acquire) {
        fail_acquire = false;
        return ESP_ERR_NO_MEM;
    }
    return __real_esp_display_present_engine_acquire_buffer(engine, lease);
}
void __real_lv_draw_buf_destroy(lv_draw_buf_t *draw);
void __wrap_lv_draw_buf_destroy(lv_draw_buf_t *draw)
{
    if (draw == watched_draw) {
        ++watched_destroyed;
    }
    __real_lv_draw_buf_destroy(draw);
}
typedef struct present_os_event present_os_event_t;
static bool forbid_wait;
bool __real_present_event_wait(present_os_event_t *, uint32_t);
bool __wrap_present_event_wait(present_os_event_t *event, uint32_t timeout)
{
    assert(!forbid_wait);
    return __real_present_event_wait(event, timeout);
}
static unsigned admission_polls;
static esp_err_t acquire_poll_result;
esp_err_t __real_esp_display_present_engine_try_begin_next_frame(esp_display_present_engine_t *, const esp_display_present_frame_request_t *);
esp_err_t __wrap_esp_display_present_engine_try_begin_next_frame(esp_display_present_engine_t *p, const esp_display_present_frame_request_t *r)
{
    ++admission_polls;
    return __real_esp_display_present_engine_try_begin_next_frame(p, r);
}
esp_err_t __real_esp_display_present_engine_try_acquire_buffer(esp_display_present_engine_t *, esp_display_present_lease_t *);
esp_err_t __wrap_esp_display_present_engine_try_acquire_buffer(esp_display_present_engine_t *p, esp_display_present_lease_t *out)
{
    if (acquire_poll_result) {
        esp_err_t result = acquire_poll_result;
        acquire_poll_result = ESP_OK;
        return result;
    }
    return __real_esp_display_present_engine_try_acquire_buffer(p, out);
}
#define W 8
#define H 6
static void count_refresh(lv_event_t *event)
{
    unsigned *counts = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_REFR_READY) {
        ++counts[0];
    }
    if (lv_event_get_code(event) == LV_EVENT_RENDER_READY) {
        ++counts[1];
    }
}
static void exercise(bool timeout, esp_display_present_present_mode_t mode)
{
    scan_panel_t panel = {0};
    esp_display_present_engine_config_t cfg = {.backend = {.width = W, .height = H, .tile_rows = 2, .buffer_count = 3, .max_frames = 4,
                                                           .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
                                                          }, .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = mode, .timeout_ms = 0
                                              };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    void *buffers[3];
    size_t count, bytes;
    OK(esp_display_present_engine_get_draw_buffers(p, buffers, 3, &count, &bytes));
    esp_lv_present_native_t *b;
#if LVGL_VERSION_MINOR < 5
    assert(esp_lv_present_native_create_zero_copy(p, &b) == ESP_ERR_NOT_SUPPORTED);
    OK(esp_lv_present_native_create(p, 2, &b));
#else
    unsigned before = canvas_allocations;
    OK(esp_lv_present_native_create_zero_copy(p, &b));
    assert(canvas_allocations == before); /* Borrowed FULL needs no pixel allocation. */
#endif
    lv_display_t *d = esp_lv_present_native_get_display(b);
    unsigned counts[2] = {0};
    lv_display_add_event_cb(d, count_refresh, LV_EVENT_ALL, counts);
    lv_obj_t *s = lv_display_get_screen_active(d);
    lv_obj_remove_style_all(s);
    lv_obj_set_style_bg_color(s, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_refr_now(d);
    OK(esp_lv_present_native_get_error(b));
    OK(esp_display_present_engine_service(p));
    bool known = false;
    for (size_t i = 0; i < count; i++) {
        known |= buffers[i] == panel.pending.pixels;
    }
    assert(known);
    assert(lv_display_get_buf_active(d)->data != panel.pending.pixels);
    uint8_t saved[W * H * 2];
    memcpy(saved, panel.pending.pixels, sizeof(saved));
    for (unsigned i = 0; i < W * H; i++) {
        uint16_t v;
        memcpy(&v, saved + 2 * i, 2);
        assert(v == 0xf800);
    }
    if (timeout) {
        /* One free slot can be queued; third admission must fail safely. */
        lv_obj_set_style_bg_color(s, lv_color_hex(0x00ff00), 0);
        lv_refr_now(d);
        const void *queued = buffers[0] == panel.pending.pixels ? buffers[1] : buffers[0];
        uint8_t queued_pixels[W * H * 2];
        memcpy(queued_pixels, queued, sizeof(queued_pixels));
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b)); /* No damage: do not request a third buffer. */
        lv_obj_set_style_bg_color(s, lv_color_hex(0x0000ff), 0);
        lv_refr_now(d);
        assert(esp_lv_present_native_get_error(b) == ESP_ERR_TIMEOUT);
        assert(!memcmp(saved, panel.pending.pixels, sizeof(saved)));
        assert(!memcmp(queued_pixels, queued, sizeof(queued_pixels)));
        OK(esp_lv_present_native_destroy(b));
        scan_confirm(&panel);
        OK(esp_display_present_engine_service(p));
        if (panel.cookie) {
            scan_confirm(&panel);
        }
    } else {
        scan_confirm(&panel);
        OK(esp_display_present_engine_drain(p, 0));
        unsigned acquires = blocking_acquires, refreshes = counts[0], renders = counts[1];
        for (unsigned i = 0; i < 3; ++i) {
            lv_refr_now(d);
        }
        assert(counts[0] == refreshes + 3 && counts[1] == renders);
        assert(blocking_acquires == acquires);
#if LVGL_VERSION_MINOR >= 5
        assert(!lv_display_get_buf_active(d)->data);
#endif
        esp_display_present_engine_status_t status;
        OK(esp_display_present_engine_get_status(p, &status));
        assert(!status.frame_active);
        forbid_wait = true;
        lv_obj_set_style_bg_color(s, lv_color_hex(0x00ff00), 0);
        forbid_wait = false;
        assert(blocking_acquires == acquires);
        lv_refr_now(d);
#if LVGL_VERSION_MINOR >= 5
        assert(blocking_acquires == acquires + (mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL));
#endif
        OK(esp_lv_present_native_get_error(b));
        OK(esp_display_present_engine_service(p));
        assert(!memcmp(saved, panel.current.pixels, sizeof(saved)));
        OK(esp_lv_present_native_destroy(b)); /* Submitted storage belongs to backend, not binding. */
        scan_confirm(&panel);
        for (unsigned i = 0; i < W * H; i++) {
            uint16_t v;
            memcpy(&v, (uint8_t*)panel.current.pixels + 2 * i, 2);
            assert(v == 0x07e0);
        }
    }
    OK(esp_display_present_engine_drain(p, 0));
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}
#if LVGL_VERSION_MINOR >= 5
/* Binding creation reserves a writable surface before mounting LVGL. An
 * immediate close must revoke it without submitting or leaking a frame. */
static void destroy_before_refresh(esp_display_present_present_mode_t mode)
{
    scan_panel_t panel = {.immediate = true};
    esp_display_present_engine_config_t cfg = {
        .backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
        },
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = mode, .timeout_ms = 1000
    };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    unsigned before = canvas_allocations;
    esp_lv_present_native_t *binding;
    OK(esp_lv_present_native_create_zero_copy(p, &binding));
    assert(canvas_allocations == before);
    esp_display_present_engine_status_t status;
    OK(esp_display_present_engine_get_status(p, &status));
    assert(status.frame_active);
    OK(esp_lv_present_native_destroy(binding));
    OK(esp_display_present_engine_get_status(p, &status));
    assert(!status.frame_active && !panel.cookie);
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}
static void pause_copy(void)
{
    const struct timespec delay = {.tv_nsec = 1000000};
    nanosleep(&delay, NULL);
}
/* A persistent logical canvas must retain disjoint damage across frames while
 * the physical candidate alternates. It must never alias current scanout. */
static void check_direct(scan_panel_t *panel, unsigned rotation, unsigned changed, bool swap)
{
    unsigned pw = rotation & 1 ? H : W;
    for (unsigned y = 0; y < H; ++y) {
        for (unsigned x = 0; x < W; ++x) {
            unsigned px = x, py = y;
            switch (rotation) {
            case 1: px = H - 1 - y; py = x; break;
            case 2: px = W - 1 - x; py = H - 1 - y; break;
            case 3: px = y; py = W - 1 - x; break;
            }
            uint16_t pixel;
            memcpy(&pixel, (uint8_t *)panel->current.pixels + (py * pw + px) * 2, 2);
            if (swap) {
                pixel = (uint16_t)((pixel << 8) | (pixel >> 8));
            }
            uint16_t expected = (changed & 1) && x >= 1 && x < 3 && y >= 1 && y < 3 ? 0xf800 :
                                (changed & 2) && x >= 5 && x < 7 && y >= 3 && y < 5 ? 0x001f : 0xffff;
            assert(pixel == expected);
        }
    }
}
static esp_err_t checked_direct_copy(void *user, const esp_display_present_copy_request_t *request)
{
    (void)user;
    uint8_t saved[W * H * 2];
    assert(request->src.capacity_bytes <= sizeof(saved));
    memcpy(saved, request->src.pixels, request->src.capacity_bytes);
    pause_copy(); /* Keep conversion alive while a following LVGL area is ready. */
    assert(!memcmp(saved, request->src.pixels, request->src.capacity_bytes));
    esp_err_t result = esp_display_present_pixel_execute_cpu(request);
    assert(!memcmp(saved, request->src.pixels, request->src.capacity_bytes));
    return result;
}
static void exercise_direct(unsigned rotation, bool async, bool swap)
{
    scan_panel_t panel = {.immediate = rotation == 0};
    esp_display_present_engine_config_t cfg = {.backend = {.width = W, .height = H, .tile_rows = 2,
                                                           .buffer_count = 2, .max_frames = 4, .rotation = rotation, .async_copy = async, .copy_execute = checked_direct_copy,
                                                           .panel_order = swap ? ESP_DISPLAY_PRESENT_ORDER_SWAPPED : ESP_DISPLAY_PRESENT_ORDER_NATIVE,
                                                           .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
                                                          },
                                               .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT,
                                               .timeout_ms = 1000
                                              };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    esp_lv_present_native_t *b;
    unsigned before = canvas_allocations;
    esp_err_t result = esp_lv_present_native_create_zero_copy(p, &b);
    assert(canvas_allocations == before); /* No canvas, including layout rejection. */
    if (rotation || swap) {
        assert(result == ESP_ERR_NOT_SUPPORTED);
        OK(esp_lv_present_native_create(p, 2, &b));
    } else {
        OK(result);
    }
    {
        lv_display_t *d = esp_lv_present_native_get_display(b);
        lv_obj_t *screen = lv_display_get_screen_active(d);
        lv_obj_remove_style_all(screen);
        lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b));
        /* Async conversion and backend worker require pumping before confirmation. */
        for (unsigned i = 0; !panel.cookie && !panel.immediate && i < 10000; ++i) {
            OK(esp_display_present_engine_service(p));
            pause_copy();
        }
        if (!panel.immediate) {
            assert(panel.cookie);
            scan_confirm(&panel);
        }
        OK(esp_display_present_engine_drain(p, 1000));
        check_direct(&panel, rotation, false, swap);
        lv_obj_t *boxes[2];
        for (unsigned i = 0; i < 2; ++i) {
            boxes[i] = lv_obj_create(screen);
            lv_obj_remove_style_all(boxes[i]);
            lv_obj_set_pos(boxes[i], i ? 5 : 1, i ? 3 : 1);
            lv_obj_set_size(boxes[i], 2, 2);
            lv_obj_set_style_bg_color(boxes[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(boxes[i], LV_OPA_COVER, 0);
        }
        unsigned changed = 0;
        for (unsigned frame = 0; frame < 16; ++frame) {
            uint8_t saved[W * H * 2];
            void *current = panel.current.pixels;
            memcpy(saved, current, sizeof(saved));
            unsigned box = frame & 1;
            changed ^= 1u << box;
            lv_obj_set_style_bg_color(boxes[box], (changed & (1u << box)) ?
                                      lv_color_hex(box ? 0x0000ff : 0xff0000) : lv_color_white(), 0);
            lv_refr_now(d);
            OK(esp_lv_present_native_get_error(b));
            for (unsigned i = 0; !panel.cookie && !panel.immediate && i < 10000; ++i) {
                OK(esp_display_present_engine_service(p));
                pause_copy();
            }
            if (panel.immediate) {
                OK(esp_display_present_engine_drain(p, 1000));
            }
            assert((panel.immediate ? panel.current.pixels : panel.pending.pixels) != current);
            assert(!memcmp(saved, current, sizeof(saved)));
            if (!panel.immediate) {
                assert(panel.cookie);
                scan_confirm(&panel);
            }
            OK(esp_display_present_engine_drain(p, 1000));
            check_direct(&panel, rotation, changed, swap);
        }
        /* No damage must not publish another frame or retain a write lease. */
        size_t submissions = panel.submissions;
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b));
        assert(panel.submissions == submissions);
        esp_display_present_engine_status_t status;
        OK(esp_display_present_engine_get_status(p, &status));
        assert(!status.frame_active);
        /* Transition from a logical canvas to physical DIRECT must remain safe. */
        panel.immediate = true;
        OK(esp_lv_present_native_set_rotation(b, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1000));
        lv_obj_clean(screen);
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b));
        for (unsigned i = 0; !panel.cookie && !panel.immediate && i < 10000; ++i) {
            OK(esp_display_present_engine_service(p));
            pause_copy();
        }
        if (!panel.immediate) {
            assert(panel.cookie);
            scan_confirm(&panel);
        }
        OK(esp_display_present_engine_drain(p, 1000));
        for (unsigned i = 0; i < W * H; ++i) {
            uint16_t pixel;
            memcpy(&pixel, (uint8_t *)panel.current.pixels + 2 * i, 2);
            assert(pixel == 0xffff);
        }
        if (!rotation && !swap) {
            unsigned before_rotation = canvas_allocations;
            OK(esp_lv_present_native_set_rotation(b, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 1000));
            assert(canvas_allocations == before_rotation + 1); /* Actual DIRECT copy source. */
            lv_refr_now(d);
            OK(esp_lv_present_native_get_error(b));
            OK(esp_display_present_engine_drain(p, 1000));
            before_rotation = canvas_allocations;
            OK(esp_lv_present_native_set_rotation(b, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1000));
            assert(canvas_allocations == before_rotation); /* Borrow again, no placeholder. */
            lv_refr_now(d);
            OK(esp_lv_present_native_get_error(b));
            OK(esp_display_present_engine_drain(p, 1000));
        }
        OK(esp_lv_present_native_destroy(b));
    }
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}
#endif

#if LVGL_VERSION_MINOR >= 5
static void rotation_borrow_failure(void)
{
    scan_panel_t panel = {.immediate = true};
    esp_display_present_engine_config_t cfg = {.backend = {.width = W, .height = H, .buffer_count = 2, .max_frames = 4,
                                                           .rotation = ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
                                                          },
                                               .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT, .timeout_ms = 1000
                                              };
    esp_display_present_engine_t *engine;
    OK(esp_display_present_engine_create(&cfg, &engine));
    esp_lv_present_native_t *binding;
    OK(esp_lv_present_native_create_zero_copy(engine, &binding));
    OK(esp_lv_present_native_set_rotation(binding, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 1000));
    lv_display_t *display = esp_lv_present_native_get_display(binding);
    watched_draw = lv_display_get_buf_active(display);
    assert(watched_draw);
    watched_destroyed = 0;
    fail_acquire = true;
    assert(esp_lv_present_native_set_rotation(binding, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1000) == ESP_ERR_NO_MEM);
    assert(!fail_acquire && watched_destroyed == 0);
    assert(lv_display_get_buf_active(display) == watched_draw);
    assert(esp_lv_present_native_get_error(binding) == ESP_ERR_NO_MEM);
    esp_display_present_engine_rotation_t rotation;
    OK(esp_display_present_engine_get_rotation(engine, &rotation));
    assert(rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0); /* Fail closed; no pretend rollback. */
    assert(esp_lv_present_native_set_rotation(binding, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 0) == ESP_ERR_INVALID_STATE);
    OK(esp_lv_present_native_destroy(binding));
    assert(watched_destroyed == 1);
    watched_draw = NULL;
    OK(esp_display_present_engine_stop(engine, 1000));
    OK(esp_display_present_engine_destroy(engine));
}
#endif

#if LVGL_VERSION_MINOR >= 5
static void timer_step(unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i) {
        lv_tick_inc(1);
        (void)lv_timer_handler();
    }
}

static void pulse(lv_timer_t *timer)
{
    ++*(unsigned *)lv_timer_get_user_data(timer);
}

static lv_timer_t *admission_timer(esp_lv_present_native_t *binding)
{
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        if (lv_timer_get_user_data(t) == binding) {
            return t;
        }
    }
    assert(false);
    return NULL;
}

static void *bind_other_owner(void *engine)
{
    OK(esp_display_present_engine_rebind_producer(engine, 0));
    return NULL;
}

/* Real LVGL damage collection, timer scheduling and two physical scan buffers.
 * Invalidate while both buffers are pinned: neither invalidation nor an
 * explicit refresh may modify pending/scanning pixels or discard damage. */
static void deferred_direct(unsigned action)
{
    scan_panel_t panel = {0};
    esp_display_present_engine_config_t cfg = {
        .backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
        },
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT,
        .timeout_ms = action == 1 ? 5 : 1000
    };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    OK(present_presenter_control_reset_metrics(p));
    esp_lv_present_native_t *binding;
    OK(esp_lv_present_native_create_zero_copy(p, &binding));
    lv_display_t *d = esp_lv_present_native_get_display(binding);
    lv_timer_t *retry = admission_timer(binding);
    assert(lv_timer_get_paused(retry));
    lv_obj_t *screen = lv_display_get_screen_active(d);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *boxes[2];
    for (unsigned i = 0; i < 2; ++i) {
        boxes[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(boxes[i]);
        lv_obj_set_pos(boxes[i], i ? 5 : 1, i ? 3 : 1);
        lv_obj_set_size(boxes[i], 2, 2);
        lv_obj_set_style_bg_color(boxes[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(boxes[i], LV_OPA_COVER, 0);
    }
    lv_refr_now(d);
    assert(panel.cookie);
    scan_confirm(&panel);
    OK(esp_display_present_engine_drain(p, 0));
    check_direct(&panel, 0, 0, false);
    lv_obj_set_style_bg_color(boxes[0], lv_color_hex(0xff0000), 0);
    lv_refr_now(d);
    assert(panel.cookie && panel.current.pixels != panel.pending.pixels);
    uint8_t current[W * H * 2], pending[W * H * 2];
    memcpy(current, panel.current.pixels, sizeof(current));
    memcpy(pending, panel.pending.pixels, sizeof(pending));
    unsigned polls = admission_polls;
    forbid_wait = true;
    lv_obj_set_style_bg_color(boxes[1], lv_color_hex(0x0000ff), 0);
    forbid_wait = false;
    assert(admission_polls == polls && lv_timer_get_paused(retry));
    forbid_wait = true;
    lv_refr_now(d);
    forbid_wait = false;
    assert(admission_polls > polls && !lv_timer_get_paused(retry));
    assert(lv_timer_get_paused(lv_display_get_refr_timer(d)));
    assert(!lv_display_get_buf_active(d)->data && d->inv_p);
    unsigned damage = d->inv_p;
    lv_refr_now(d); /* Manual refresh must retain damage while admission is pending. */
    assert(d->inv_p == damage && !lv_display_get_buf_active(d)->data);
    unsigned pulses = 0;
    lv_timer_t *animation = lv_timer_create(pulse, 1, &pulses);
    forbid_wait = true;
    timer_step(3);
    forbid_wait = false;
    assert(pulses >= 3);
    OK(esp_lv_present_native_get_error(binding));
    lv_obj_set_style_bg_color(boxes[0], lv_color_white(), 0);
    assert(!memcmp(current, panel.current.pixels, sizeof(current)));
    assert(!memcmp(pending, panel.pending.pixels, sizeof(pending)));
    if (action == 1) {
        /* A second invalidation must not restart the total admission deadline. */
        timer_step(2);
        assert(esp_lv_present_native_get_error(binding) == ESP_ERR_TIMEOUT);
        assert(lv_timer_get_paused(retry) && !lv_display_get_buf_active(d)->data);
        polls = admission_polls;
        scan_confirm(&panel); /* Late retirement cannot revive the faulted renderer. */
        timer_step(3);
        assert(admission_polls == polls);
        OK(esp_lv_present_native_destroy(binding));
    } else if (action == 2) {
        /* Destroy an outstanding request before handing off the producer. */
        lv_timer_delete(animation);
        animation = NULL;
        polls = admission_polls;
        OK(esp_lv_present_native_destroy(binding));
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, bind_other_owner, p));
        assert(!pthread_join(thread, NULL));
        timer_step(5);
        assert(admission_polls == polls);
        OK(esp_display_present_engine_rebind_producer(p, 0));
        scan_confirm(&panel);
    } else {
        if (action == 3) {
            assert(esp_lv_present_native_set_rotation(binding, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 0) == ESP_ERR_TIMEOUT);
            assert(!lv_timer_get_paused(retry));
            OK(esp_lv_present_native_get_error(binding));
        }
        scan_confirm(&panel);
        if (action == 4) {
            /* A successful control transition cancels the stale retry before
             * changing storage/geometry. The copy binding owns its new canvas. */
            polls = admission_polls;
            OK(esp_lv_present_native_set_rotation(binding, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 1000));
            assert(lv_timer_get_paused(retry));
            lv_refr_now(d);
            timer_step(1);
            assert(admission_polls == polls);
        } else {
            timer_step(1);
            if (!panel.cookie) {
                lv_refr_now(d);
            }
        }
        OK(esp_lv_present_native_get_error(binding));
        assert(panel.cookie);
        scan_confirm(&panel);
        OK(esp_display_present_engine_drain(p, 0));
        if (action != 4) {
            check_direct(&panel, 0, 2, false); /* Both outstanding changes survived. */
        }
        lv_timer_delete(animation);
        animation = NULL;
        polls = admission_polls;
        timer_step(5);
        assert(lv_timer_get_paused(retry) && admission_polls == polls); /* No idle polling. */
        OK(esp_lv_present_native_destroy(binding));
    }
    if (animation) {
        lv_timer_delete(animation);
    }
    OK(esp_display_present_engine_drain(p, 1000));
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}

/* Rollback preserves an unrendered full invalidation. LVGL coalesces the
 * repeated invalidation without emitting a request. */
static void pending_rotation_timeout(void)
{
    scan_panel_t panel = {0};
    esp_display_present_engine_config_t cfg = {
        .backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
        },
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT, .timeout_ms = 1000
    };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    esp_lv_present_native_t *b;
    OK(esp_lv_present_native_create_zero_copy(p, &b));
    lv_display_t *d = esp_lv_present_native_get_display(b);
    lv_refr_now(d);
    assert(panel.cookie);
    lv_obj_set_style_bg_color(lv_display_get_screen_active(d), lv_color_hex(0x0000ff), 0);
    assert(!lv_display_get_buf_active(d)->data && d->inv_p);
    assert(esp_lv_present_native_set_rotation(b, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 0) == ESP_ERR_TIMEOUT);
    assert(lv_display_get_buf_active(d)->data || !lv_timer_get_paused(admission_timer(b)));
    scan_confirm(&panel);
    timer_step(1);
    lv_refr_now(d);
    OK(esp_lv_present_native_get_error(b));
    assert(panel.cookie);
    scan_confirm(&panel);
    OK(esp_lv_present_native_destroy(b));
    OK(esp_display_present_engine_drain(p, 0));
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}

/* Hardware transfer expiry is a terminal fault, never admission backpressure. */
static void deferred_transfer_fault(void)
{
    scan_panel_t panel = {0};
    esp_display_present_engine_config_t cfg = {
        .backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
        },
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT,
        .timeout_ms = 1000, .transfer_timeout_ms = 1
    };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    esp_lv_present_native_t *b;
    OK(esp_lv_present_native_create_zero_copy(p, &b));
    lv_display_t *d = esp_lv_present_native_get_display(b);
    lv_refr_now(d);
    assert(panel.cookie);
    struct timespec delay = {.tv_nsec = 5000000};
    nanosleep(&delay, NULL);
    lv_obj_set_style_bg_color(lv_display_get_screen_active(d), lv_color_hex(0x0000ff), 0);
    /* Check before get_error(), which can independently discover the fault. */
    assert(lv_timer_get_paused(admission_timer(b)));
    assert(esp_lv_present_native_get_error(b) == ESP_ERR_TIMEOUT);
    OK(esp_lv_present_native_destroy(b));
    scan_confirm(&panel);
    esp_err_t drained = esp_display_present_engine_drain(p, 0);
    assert(drained == ESP_OK || drained == ESP_ERR_TIMEOUT);
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}

static void deferred_acquire(esp_err_t injected)
{
    scan_panel_t panel = {.immediate = true};
    esp_display_present_engine_config_t cfg = {
        .backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
        },
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT, .timeout_ms = 1000
    };
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    esp_lv_present_native_t *b;
    OK(esp_lv_present_native_create_zero_copy(p, &b));
    lv_display_t *d = esp_lv_present_native_get_display(b);
    lv_refr_now(d);
    OK(esp_display_present_engine_drain(p, 0));
    acquire_poll_result = injected;
    lv_obj_set_style_bg_color(lv_display_get_screen_active(d), lv_color_hex(0x0000ff), 0);
    assert(acquire_poll_result == injected);
    lv_refr_now(d);
    assert(!acquire_poll_result);
    if (injected == ESP_ERR_NOT_FINISHED) {
        assert(!lv_timer_get_paused(admission_timer(b)));
        timer_step(1);
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b));
    } else {
        assert(esp_lv_present_native_get_error(b) == injected);
    }
    OK(esp_lv_present_native_destroy(b));
    OK(esp_display_present_engine_drain(p, 0));
    OK(esp_display_present_engine_stop(p, 1000));
    OK(esp_display_present_engine_destroy(p));
}
#endif

int main(void)
{
    lv_init();
#if LVGL_VERSION_MINOR >= 5
    rotation_borrow_failure();
    for (unsigned action = 0; action < 5; ++action) {
        deferred_direct(action);
    }
    pending_rotation_timeout();
    deferred_transfer_fault();
    deferred_acquire(ESP_ERR_NOT_FINISHED);
    deferred_acquire(ESP_ERR_NO_MEM);
#endif
    exercise(false, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL);
    exercise(true, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL);
    exercise(false, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT);
    exercise(true, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT);
#if LVGL_VERSION_MINOR >= 5
    destroy_before_refresh(ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL);
    destroy_before_refresh(ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT);
    for (unsigned r = 0; r < 4; ++r) {
        exercise_direct(r, false, false);
        exercise_direct(r, true, false);
        exercise_direct(r, false, true);
        exercise_direct(r, true, true);
    }
#endif
    lv_deinit();
    /* Reinitialization must not leave a stale custom event or binding lookup. */
    lv_init();
    exercise(false, ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL);
    lv_deinit();
    puts("LVGL zero-copy: lease pixels, detach, backpressure and destroy lifetime passed");
}
