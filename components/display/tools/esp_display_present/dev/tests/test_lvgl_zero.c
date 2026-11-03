/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lvgl.h"
#if LVGL_VERSION_MINOR == 0
#include "src/display/lv_display_private.h"
#define lv_display_get_buf_active(display) ((display)->buf_act)
#endif
#include "scan_panel.h"
#include <stdio.h>
#define OK(x) assert((x)==ESP_OK)
#define W 8
#define H 6
static void exercise(bool timeout)
{
    scan_panel_t panel = {0};
    esp_display_present_engine_config_t cfg = {.backend = {.width = W, .height = H, .tile_rows = 2, .buffer_count = 3, .max_frames = 4,
                                                           .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = scan_interface(&panel)
                                                          }, .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL, .timeout_ms = 0
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
    OK(esp_lv_present_native_create_zero_copy(p, &b));
#endif
    lv_display_t *d = esp_lv_present_native_get_display(b);
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
        lv_refr_now(d);
        OK(esp_lv_present_native_get_error(b)); /* No damage: do not request a third buffer. */
        lv_obj_set_style_bg_color(s, lv_color_hex(0x0000ff), 0);
        lv_refr_now(d);
        assert(esp_lv_present_native_get_error(b) == ESP_ERR_TIMEOUT);
        assert(!memcmp(saved, panel.pending.pixels, sizeof(saved)));
        OK(esp_lv_present_native_destroy(b));
        scan_confirm(&panel);
        OK(esp_display_present_engine_service(p));
        if (panel.cookie) {
            scan_confirm(&panel);
        }
    } else {
        scan_confirm(&panel);
        OK(esp_display_present_engine_drain(p, 0));
        lv_refr_now(d); /* No invalidations must return the unused lease. */
        esp_display_present_engine_status_t status;
        OK(esp_display_present_engine_get_status(p, &status));
        assert(!status.frame_active);
        lv_obj_set_style_bg_color(s, lv_color_hex(0x00ff00), 0);
        lv_refr_now(d);
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
int main(void)
{
    lv_init();
    exercise(false);
    exercise(true);
    lv_deinit();
    puts("LVGL zero-copy: lease pixels, detach, backpressure and destroy lifetime passed");
}
