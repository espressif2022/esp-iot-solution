/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lvgl.h"
#include "sync_panel.h"
#include <stdio.h>
#define OK(x) assert((x) == ESP_OK)
#define WIDTH 7
#define HEIGHT 9

static pv2_presenter_t *create(sync_panel_t *s, pv2_presenter_backend_t kind, uint32_t timeout)
{
    pv2_presenter_config_t config = {.backend = {
            .width = s->panel.width, .height = s->panel.height, .tile_rows = 2,
            .buffer_count = 2, .max_frames = 2, .format = s->panel.bpp == 2 ? PV2_FORMAT_RGB565 : PV2_FORMAT_RGB888,
            .panel = sync_panel_interface(s), .te_window_us = 1000000, .te_transfer_budget_us = 1000
        },
        .backend_kind = kind, .timeout_ms = timeout
    };
    pv2_presenter_t *p;
    OK(pv2_presenter_create(&config, &p));
    return p;
}

static void background(lv_display_t *display, uint32_t color)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static lv_obj_t *box(lv_display_t *display, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(lv_display_get_screen_active(display));
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, w, h);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

static void check_image(sync_panel_t *s, unsigned phase)
{
    /* Independent image oracle, including separated dirty rectangles and old
     * rectangle restoration when a box moves. */
    pthread_mutex_lock(&s->mutex);
    for (unsigned y = 0; y < HEIGHT; y++) {
        for (unsigned x = 0; x < WIDTH; x++) {
            uint32_t rgb = 0x123456;
            unsigned left = phase == 2 ? 2 : 1;
            if (phase && x >= left && x < left + 2 && y >= 1 && y < 3) {
                rgb = 0xff0000;
            }
            if (phase && x >= 4 && x < 6 && y >= 6 && y < 8) {
                rgb = 0x0000ff;
            }
            size_t offset = (y * WIDTH + x) * s->panel.bpp;
            if (s->panel.bpp == 2) {
                uint16_t expected = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 31));
                uint16_t actual;
                memcpy(&actual, s->panel.pixels + offset, 2);
                assert(actual == expected);
            } else {
                assert(s->panel.pixels[offset] == (rgb >> 16));
                assert(s->panel.pixels[offset + 1] == ((rgb >> 8) & 255));
                assert(s->panel.pixels[offset + 2] == (rgb & 255));
            }
        }
    }
    pthread_mutex_unlock(&s->mutex);
}

static void finish(sync_panel_t *s, pv2_presenter_t *p, pv2_presenter_backend_t kind)
{
    if (kind == PV2_PRESENTER_TE) {
        pthread_t worker = sync_start_worker(s, p);
        OK(pv2_presenter_drain(p, 1000));
        assert(pthread_join(worker, NULL) == 0);
    } else {
        while (s->panel.count) {
            sync_complete(s);
        }
        OK(pv2_presenter_drain(p, 0));
    }
}

static void images(pv2_presenter_backend_t kind, size_t bpp)
{
    sync_panel_t s;
    sync_panel_init(&s);
    s.panel.width = WIDTH;
    s.panel.height = HEIGHT;
    s.panel.bpp = bpp;
    s.panel.service_previous = true;
    pv2_presenter_t *p = create(&s, kind, 1000);
    pv2_lvgl_t *binding;
    OK(pv2_lvgl_create(p, 4, &binding)); /* Split LVGL bands across 2-row leases. */
    lv_display_t *display = pv2_lvgl_get_display(binding);
    background(display, 0x123456);
    lv_refr_now(display);
    OK(pv2_lvgl_get_error(binding));
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.committed_frames == 1);
    if (kind == PV2_PRESENTER_TE) {
        assert(s.panel.submitted == 0);
    } else {
        assert(s.panel.submitted >= 5);
    }
    lv_draw_buf_t *draw = lv_display_get_buf_active(display);
    memset(draw->data, 0, draw->data_size);
    finish(&s, p, kind);
    check_image(&s, 0);
    lv_obj_t *red = box(display, 1, 1, 2, 2, 0xff0000);
    (void)box(display, 4, 6, 2, 2, 0x0000ff);
    lv_refr_now(display);
    OK(pv2_lvgl_get_error(binding));
    finish(&s, p, kind);
    check_image(&s, 1);
    lv_obj_set_pos(red, 2, 1);
    lv_refr_now(display);
    OK(pv2_lvgl_get_error(binding));
    memset(draw->data, 0xcc, draw->data_size);
    /* Destroy LVGL before the last committed transmission completes. */
    OK(pv2_lvgl_destroy(binding));
    finish(&s, p, kind);
    check_image(&s, 2);
    OK(pv2_presenter_get_status(p, &status));
    assert(status.completed_frames == 3 && status.first_error == ESP_OK);
    OK(pv2_presenter_stop(p, 0));
    OK(pv2_presenter_destroy(p));
    sync_panel_deinit(&s);
}

static void backpressure(uint32_t timeout, bool deliver)
{
    sync_panel_t s;
    sync_panel_init(&s);
    pv2_presenter_t *p = create(&s, PV2_PRESENTER_TE, timeout);
    pv2_lvgl_t *binding;
    OK(pv2_lvgl_create(p, 4, &binding));
    lv_display_t *display = pv2_lvgl_get_display(binding);
    background(display, 0xffffff);
    lv_refr_now(display);
    pthread_t worker;
    if (deliver) {
        worker = sync_start_worker(&s, p);
    }
    background(display, 0xff0000);
    lv_refr_now(display); /* Waits for the first frame or explicitly pauses. */
    if (deliver) {
        assert(pthread_join(worker, NULL) == 0);
        OK(pv2_lvgl_get_error(binding));
        pv2_presenter_status_t status;
        OK(pv2_presenter_get_status(p, &status));
        assert(status.completed_frames == 1 && status.committed_frames == 2);
        finish(&s, p, PV2_PRESENTER_TE);
    } else {
        assert(pv2_lvgl_get_error(binding) == ESP_ERR_TIMEOUT);
        pv2_presenter_status_t status;
        OK(pv2_presenter_get_status(p, &status));
        assert(!status.frame_active && status.pending_frames == 1 && status.first_error == ESP_OK);
        lv_tick_inc(100);
        lv_timer_handler();
        OK(pv2_presenter_get_status(p, &status));
        assert(status.committed_frames == 1); /* Paused, no silent retries. */
        finish(&s, p, PV2_PRESENTER_TE);
        for (unsigned i = 0; i < 32; i++) {
            assert(s.panel.pixels[i] == 255);
        }
    }
    OK(pv2_lvgl_destroy(binding));
    OK(pv2_presenter_stop(p, 0));
    OK(pv2_presenter_destroy(p));
    sync_panel_deinit(&s);
}

static void multiple_instances(void)
{
    sync_panel_t a, b;
    sync_panel_init(&a);
    sync_panel_init(&b);
    a.panel.immediate = b.panel.immediate = true;
    pv2_presenter_t *pa = create(&a, PV2_PRESENTER_TE, 0);
    pv2_presenter_t *pb = create(&b, PV2_PRESENTER_GRAM, 0);
    pv2_lvgl_t *ba, *bb;
    OK(pv2_lvgl_create(pa, 2, &ba));
    OK(pv2_lvgl_create(pb, 2, &bb));
    background(pv2_lvgl_get_display(ba), 0xffffff);
    background(pv2_lvgl_get_display(bb), 0x000000);
    lv_refr_now(pv2_lvgl_get_display(ba));
    lv_refr_now(pv2_lvgl_get_display(bb));
    pv2_presenter_notify_te(pb, test_now_us()); /* GRAM ignores notification. */
    assert(!pv2_presenter_notify_te_isr(pb, test_now_us()));
    OK(pv2_presenter_service(pb));
    assert(a.panel.submitted == 0);
    pv2_presenter_notify_te(pa, test_now_us());
    OK(pv2_presenter_service(pa));
    for (unsigned i = 0; i < 32; i++) {
        assert(a.panel.pixels[i] == 255 && b.panel.pixels[i] == 0);
    }
    OK(pv2_lvgl_destroy(ba));
    OK(pv2_lvgl_destroy(bb));
    OK(pv2_presenter_stop(pa, 0));
    OK(pv2_presenter_stop(pb, 0));
    OK(pv2_presenter_destroy(pa));
    OK(pv2_presenter_destroy(pb));
    sync_panel_deinit(&a);
    sync_panel_deinit(&b);
}
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count;
} copy_notifications_t;
static bool copy_notification(void *user, bool isr)
{
    assert(!isr);
    copy_notifications_t *n = user;
    pthread_mutex_lock(&n->lock);
    n->count++;
    pthread_cond_broadcast(&n->changed);
    pthread_mutex_unlock(&n->lock);
    return false;
}

static void background_sender(bool async, pv2_presenter_backend_t kind)
{
    copy_notifications_t n = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    sync_panel_t panel;
    sync_panel_init(&panel);
    pv2_presenter_config_t config = {.backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
            .format = PV2_FORMAT_RGB565, .panel = sync_panel_interface(&panel),
            .async_copy = async, .on_progress = copy_notification, .progress_user = &n,
            .te_window_us = 1000000, .te_transfer_budget_us = 1000
        },
        .backend_kind = kind, .timeout_ms = 1000, .background_service = true
    };
    pv2_presenter_t *p;
    OK(pv2_presenter_create(&config, &p));
    pv2_lvgl_t *binding;
    OK(pv2_lvgl_create(p, 4, &binding));
    lv_display_t *display = pv2_lvgl_get_display(binding);
    background(display, 0xffffff);
    lv_refr_now(display);
    if (async) {
        pthread_mutex_lock(&n.lock);
        while (n.count < 2) {
            pthread_cond_wait(&n.changed, &n.lock);
        }
        pthread_mutex_unlock(&n.lock);
        /* Finish COPY_DRAIN before injecting the next physical TE edge. */
        OK(pv2_presenter_service(p));
    }
    if (kind == PV2_PRESENTER_TE) {
        pv2_presenter_notify_te_isr(p, test_now_us());
    }
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline); /* sync_panel's cond uses default clock. */
    deadline.tv_sec += 2;
    pthread_mutex_lock(&panel.mutex);
    size_t expected = kind == PV2_PRESENTER_TE ? 1 : 2;
    while (panel.panel.submitted < expected) {
        assert(pthread_cond_timedwait(&panel.changed, &panel.mutex, &deadline) == 0);
    }
    assert(panel.panel.count == expected);
    pthread_mutex_unlock(&panel.mutex);
    /* No owner service call was needed. DMA still references composed pixels. */
    lv_draw_buf_t *draw = lv_display_get_buf_active(display);
    memset(draw->data, 0, draw->data_size);
    for (size_t i = 0; i < expected; ++i) {
        sync_complete(&panel);
    }
    OK(pv2_presenter_drain(p, 1000));
    OK(pv2_lvgl_get_error(binding));
    pthread_mutex_lock(&panel.mutex);
    for (unsigned i = 0; i < 32; i++) {
        assert(panel.panel.pixels[i] == 255);
    }
    pthread_mutex_unlock(&panel.mutex);
    OK(pv2_lvgl_destroy(binding));
    OK(pv2_presenter_stop(p, 1000));
    OK(pv2_presenter_destroy(p));
    sync_panel_deinit(&panel);
    pthread_cond_destroy(&n.changed);
    pthread_mutex_destroy(&n.lock);
}
int main(void)
{
    lv_init();
    images(PV2_PRESENTER_GRAM, 2);
    images(PV2_PRESENTER_TE, 2);
    images(PV2_PRESENTER_GRAM, 3);
    images(PV2_PRESENTER_TE, 3);
    backpressure(0, false);
    backpressure(5, false);
    backpressure(1000, true);
    multiple_instances();
    background_sender(false, PV2_PRESENTER_TE);
    background_sender(true, PV2_PRESENTER_TE);
    background_sender(true, PV2_PRESENTER_GRAM);
    lv_deinit();
    puts("LVGL GRAM/TE: reference images, padding, source lifetime, wait/pause and isolation passed");
    return 0;
}
