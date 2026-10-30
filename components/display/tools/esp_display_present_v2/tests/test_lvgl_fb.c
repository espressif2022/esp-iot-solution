/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lvgl.h"
#include "scan_panel.h"
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#define OK(x) assert((x) == ESP_OK)
#define W 7
#define H 9
static void check(scan_panel_t *panel, size_t bpp, bool changed)
{
    for (unsigned y = 0; y < H; y++) {
        for (unsigned x = 0; x < W; x++) {
            uint32_t rgb = 0x123456;
            if (changed && x >= 1 && x < 3 && y >= 1 && y < 3) {
                rgb = 0xff0000;
            }
            if (changed && x >= 4 && x < 6 && y >= 6 && y < 8) {
                rgb = 0x0000ff;
            }
            const uint8_t *pixel = (const uint8_t *)panel->current.pixels + (y * W + x) * bpp;
            if (bpp == 2) {
                uint16_t actual, expected = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x7e0) | ((rgb >> 3) & 31));
                memcpy(&actual, pixel, 2);
                assert(actual == expected);
            } else {
                assert(pixel[0] == (rgb >> 16) && pixel[1] == ((rgb >> 8) & 255) && pixel[2] == (rgb & 255));
            }
        }
    }
}

static void box(lv_obj_t *screen, int x, int y, uint32_t color)
{
    lv_obj_t *object = lv_obj_create(screen);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, 2, 2);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count;
} notifications_t;
static bool progress(void *user, bool isr)
{
    assert(!isr);
    notifications_t *n = user;
    pthread_mutex_lock(&n->lock);
    n->count++;
    pthread_cond_broadcast(&n->changed);
    pthread_mutex_unlock(&n->lock);
    return false;
}

static void wait_scan_submit(pv2_presenter_t *p, scan_panel_t *panel, notifications_t *n)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    for (;;) {
        pthread_mutex_lock(&n->lock);
        unsigned seen = n->count;
        pthread_mutex_unlock(&n->lock);
        OK(pv2_presenter_service(p));
        if (panel->cookie) {
            return;
        }
        pthread_mutex_lock(&n->lock);
        while (seen == n->count) {
            assert(pthread_cond_timedwait(&n->changed, &n->lock, &deadline) == 0);
        }
        pthread_mutex_unlock(&n->lock);
    }
}

static void exercise(pv2_present_mode_t mode, size_t bpp, bool async)
{
    notifications_t n = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    scan_panel_t panel = {0};
    pv2_presenter_config_t cfg = {.backend = {
            .width = W, .height = H, .tile_rows = 2, .buffer_count = 3, .max_frames = 4,
            .format = bpp == 2 ? PV2_FORMAT_RGB565 : PV2_FORMAT_RGB888, .allow_active_scanout_writes = true,
            .panel = scan_interface(&panel), .async_copy = async, .on_progress = progress, .progress_user = &n
        },
        .timeout_ms = 1000, .backend_kind = PV2_PRESENTER_FB, .mode = mode
    };
    pv2_presenter_t *p;
    OK(pv2_presenter_create(&cfg, &p));
    pv2_lvgl_t *binding;
    OK(pv2_lvgl_create(p, 4, &binding));
    lv_display_t *display = pv2_lvgl_get_display(binding);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x123456), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_refr_now(display);
    OK(pv2_lvgl_get_error(binding));
    wait_scan_submit(p, &panel, &n);
    assert(panel.cookie && !panel.current.pixels);
    scan_confirm(&panel);
    OK(pv2_presenter_drain(p, 0));
    check(&panel, bpp, false);
    void *original = panel.current.pixels;
    uint8_t snapshot[W * H * 3];
    memcpy(snapshot, original, W * H * bpp);
    box(screen, 1, 1, 0xff0000);
    box(screen, 4, 6, 0x0000ff);
    lv_refr_now(display);
    OK(pv2_lvgl_get_error(binding));
    wait_scan_submit(p, &panel, &n);
    assert(panel.cookie);
    if (mode != PV2_MODE_NONE) {
        assert(panel.pending.pixels != original);
        assert(memcmp(original, snapshot, W * H * bpp) == 0);
    }
    /* Completed receipts do not retire original until the switch confirmation. */
    assert(pv2_presenter_drain(p, 0) == ESP_ERR_TIMEOUT);
    OK(pv2_lvgl_destroy(binding)); /* Persistent LVGL DIRECT buffer no longer needed. */
    scan_confirm(&panel);
    OK(pv2_presenter_drain(p, 0));
    check(&panel, bpp, true);
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.completed_frames == 2 && status.first_error == ESP_OK);
    panel.stop_timeout = true;
    assert(pv2_presenter_stop(p, 0) == ESP_ERR_TIMEOUT);
    panel.stop_timeout = false;
    OK(pv2_presenter_stop(p, 1000));
    OK(pv2_presenter_destroy(p));
    pthread_cond_destroy(&n.changed);
    pthread_mutex_destroy(&n.lock);
}
int main(void)
{
    const pv2_present_mode_t modes[] = {PV2_MODE_NONE, PV2_MODE_DOUBLE_FULL, PV2_MODE_TRIPLE_FULL,
                                        PV2_MODE_DOUBLE_DIRECT, PV2_MODE_DOUBLE_PARTIAL, PV2_MODE_TRIPLE_PARTIAL, PV2_MODE_AUTO
                                       };
    lv_init();
    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        exercise(modes[i], 2, false);
        exercise(modes[i], 3, false);
    }
    exercise(PV2_MODE_DOUBLE_PARTIAL, 2, true);
    exercise(PV2_MODE_TRIPLE_PARTIAL, 3, true);
    exercise(PV2_MODE_DOUBLE_DIRECT, 2, true);
    exercise(PV2_MODE_DOUBLE_DIRECT, 3, true);
    lv_deinit();
    puts("LVGL FB modes: FULL/DIRECT/PARTIAL, RGB565/888, dirty offsets and scanout lifetime passed");
    return 0;
}
