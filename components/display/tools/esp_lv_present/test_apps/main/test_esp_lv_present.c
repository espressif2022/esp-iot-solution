/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_display_present.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_log.h"
#include "esp_lv_present.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"

static const char *TAG = "lv_present_test";

#define TEST_LCD_H_RES              120
#define TEST_LCD_V_RES              80
#define TEST_MEMORY_LEAK_THRESHOLD  (2000)
#define TEST_QUIESCE_TIMEOUT_MS     (2000)

typedef struct {
    esp_lcd_panel_io_t base;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
    esp_timer_handle_t done_timer;
} dummy_io_t;

typedef struct {
    esp_lcd_panel_t base;
} dummy_panel_t;

static dummy_io_t s_dummy_io;
static dummy_panel_t s_dummy_panel;
static uint16_t s_panel_pixels[TEST_LCD_H_RES * TEST_LCD_V_RES];

static void IRAM_ATTR dummy_color_done_timer(void *arg)
{
    dummy_io_t *io = arg;
    if (io->on_color_trans_done != NULL) {
        (void)io->on_color_trans_done(&io->base, NULL, io->user_ctx);
    }
}

static esp_err_t dummy_io_register_event_callbacks(
    esp_lcd_panel_io_t *io,
    const esp_lcd_panel_io_callbacks_t *cbs,
    void *user_ctx)
{
    dummy_io_t *dummy = (dummy_io_t *)io;
    dummy->on_color_trans_done = (cbs != NULL) ? cbs->on_color_trans_done : NULL;
    dummy->user_ctx = user_ctx;
    return ESP_OK;
}

static esp_err_t dummy_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start,
                                   int x_end, int y_end, const void *color_data)
{
    (void)panel;
    if (color_data != NULL && x_start >= 0 && y_start >= 0 &&
            x_end <= TEST_LCD_H_RES && y_end <= TEST_LCD_V_RES &&
            x_start < x_end && y_start < y_end) {
        const uint16_t *src = (const uint16_t *)color_data;
        const int width = x_end - x_start;
        for (int y = y_start; y < y_end; ++y) {
            memcpy(&s_panel_pixels[y * TEST_LCD_H_RES + x_start],
                   &src[(y - y_start) * width],
                   (size_t)width * sizeof(uint16_t));
        }
    }
    (void)esp_timer_stop(s_dummy_io.done_timer);
    (void)esp_timer_start_once(s_dummy_io.done_timer, 0);
    return ESP_OK;
}

static void dummy_panel_init(void)
{
    memset(&s_dummy_io, 0, sizeof(s_dummy_io));
    memset(&s_dummy_panel, 0, sizeof(s_dummy_panel));
    memset(s_panel_pixels, 0, sizeof(s_panel_pixels));
    s_dummy_io.base.register_event_callbacks = dummy_io_register_event_callbacks;
    s_dummy_panel.base.draw_bitmap = dummy_draw_bitmap;

    const esp_timer_create_args_t timer_args = {
        .callback = dummy_color_done_timer,
        .arg = &s_dummy_io,
        .dispatch_method = ESP_TIMER_ISR,
        .name = "dummy_lcd_done",
    };
    TEST_ESP_OK(esp_timer_create(&timer_args, &s_dummy_io.done_timer));
}

static void dummy_panel_deinit(void)
{
    if (s_dummy_io.done_timer != NULL) {
        (void)esp_timer_stop(s_dummy_io.done_timer);
        TEST_ESP_OK(esp_timer_delete(s_dummy_io.done_timer));
        s_dummy_io.done_timer = NULL;
    }
}

static void test_presenter_create(esp_display_presenter_t **out_presenter)
{
    dummy_panel_init();

    const esp_display_present_target_config_t target = {
        .hw = {
            .panel = &s_dummy_panel.base,
            .io = &s_dummy_io.base,
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
            .rotation = ESP_DISPLAY_PRESENT_ROTATE_0,
            .swap_bytes = false,
        },
        .fb = {
            .mode = ESP_DISPLAY_PRESENT_MODE_NONE,
        },
    };
    const esp_display_presenter_config_t config = {
        .width = TEST_LCD_H_RES,
        .height = TEST_LCD_V_RES,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = 8,
        .target = target,
    };
    TEST_ESP_OK(esp_display_presenter_create(&config, out_presenter));
    TEST_ASSERT_NOT_NULL(*out_presenter);
}

static void test_presenter_delete(esp_display_presenter_t *presenter)
{
    if (presenter != NULL) {
        TEST_ESP_OK(esp_display_presenter_delete(presenter));
    }
    dummy_panel_deinit();
}

static size_t before_free_8bit;
static size_t before_free_32bit;

void setUp(void)
{
    before_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

void tearDown(void)
{
    size_t after_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    unity_utils_check_leak(before_free_8bit, after_free_8bit, "8BIT", TEST_MEMORY_LEAK_THRESHOLD);
    unity_utils_check_leak(before_free_32bit, after_free_32bit, "32BIT", TEST_MEMORY_LEAK_THRESHOLD);
}

TEST_CASE("lv present start/stop arguments", "[lv_present][basic]")
{
    esp_display_presenter_t *presenter = NULL;
    lv_display_t *disp = NULL;

    TEST_ESP_ERR(ESP_ERR_INVALID_ARG, esp_lv_present_start(NULL, &disp));
    TEST_ESP_ERR(ESP_ERR_INVALID_STATE, esp_lv_present_stop());

    test_presenter_create(&presenter);
    TEST_ESP_ERR(ESP_ERR_INVALID_ARG, esp_lv_present_start(presenter, NULL));
    TEST_ESP_OK(esp_lv_present_start(presenter, &disp));
    TEST_ASSERT_NOT_NULL(disp);
    TEST_ESP_ERR(ESP_ERR_INVALID_STATE, esp_lv_present_start(presenter, &disp));

    TEST_ESP_OK(esp_display_presenter_quiesce(presenter, TEST_QUIESCE_TIMEOUT_MS));
    TEST_ESP_OK(esp_lv_present_stop());
    test_presenter_delete(presenter);
}

TEST_CASE("lv present flushes through presenter", "[lv_present][basic]")
{
    esp_display_presenter_t *presenter = NULL;
    lv_display_t *disp = NULL;
    test_presenter_create(&presenter);

    TEST_ESP_OK(esp_lv_present_start(presenter, &disp));
    TEST_ASSERT_NOT_NULL(disp);

    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(disp));
    lv_label_set_text(label, "esp_lv_present");
    lv_obj_center(label);
    lv_refr_now(disp);

    TEST_ASSERT_TRUE(esp_lv_present_get_frame_count() > 0);
    ESP_LOGI(TAG, "committed %" PRIu32 " frame(s)", esp_lv_present_get_frame_count());

    TEST_ESP_OK(esp_display_presenter_quiesce(presenter, TEST_QUIESCE_TIMEOUT_MS));
    TEST_ESP_OK(esp_lv_present_stop());
    test_presenter_delete(presenter);
}

TEST_CASE("lv present runtime set_rotation", "[lv_present][rotation]")
{
    static const esp_display_present_rotation_t angles[] = {
        ESP_DISPLAY_PRESENT_ROTATE_0,
        ESP_DISPLAY_PRESENT_ROTATE_90,
        ESP_DISPLAY_PRESENT_ROTATE_180,
        ESP_DISPLAY_PRESENT_ROTATE_270,
        ESP_DISPLAY_PRESENT_ROTATE_0,
    };
    esp_display_presenter_t *presenter = NULL;
    lv_display_t *disp = NULL;

    TEST_ESP_ERR(ESP_ERR_INVALID_STATE,
                 esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, TEST_QUIESCE_TIMEOUT_MS));

    test_presenter_create(&presenter);
    TEST_ESP_OK(esp_lv_present_start(presenter, &disp));
    TEST_ASSERT_NOT_NULL(disp);

    TEST_ESP_ERR(ESP_ERR_INVALID_ARG,
                 esp_lv_present_set_rotation((esp_display_present_rotation_t)45, TEST_QUIESCE_TIMEOUT_MS));

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *marker = lv_obj_create(scr);
    lv_obj_remove_style_all(marker);
    lv_obj_set_size(marker, 16, 8);
    lv_obj_set_pos(marker, 0, 0);
    lv_obj_set_style_bg_color(marker, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);

    for (size_t i = 0; i < sizeof(angles) / sizeof(angles[0]); ++i) {
        const esp_display_present_rotation_t rot = angles[i];
        const int32_t expected_w = (rot == ESP_DISPLAY_PRESENT_ROTATE_90 ||
                                    rot == ESP_DISPLAY_PRESENT_ROTATE_270)
                                   ? TEST_LCD_V_RES : TEST_LCD_H_RES;
        const int32_t expected_h = (rot == ESP_DISPLAY_PRESENT_ROTATE_90 ||
                                    rot == ESP_DISPLAY_PRESENT_ROTATE_270)
                                   ? TEST_LCD_H_RES : TEST_LCD_V_RES;
        uint32_t before_frames = esp_lv_present_get_frame_count();

        TEST_ESP_OK(esp_lv_present_set_rotation(rot, TEST_QUIESCE_TIMEOUT_MS));
        TEST_ASSERT_EQUAL_INT32(expected_w, lv_display_get_horizontal_resolution(disp));
        TEST_ASSERT_EQUAL_INT32(expected_h, lv_display_get_vertical_resolution(disp));

        esp_display_presenter_caps_t caps = {0};
        TEST_ESP_OK(esp_display_presenter_get_caps(presenter, &caps));
        TEST_ASSERT_EQUAL_INT32(expected_w, caps.width);
        TEST_ASSERT_EQUAL_INT32(expected_h, caps.height);

        memset(s_panel_pixels, 0, sizeof(s_panel_pixels));
        lv_obj_invalidate(scr);
        lv_refr_now(disp);
        TEST_ESP_OK(esp_display_presenter_quiesce(presenter, TEST_QUIESCE_TIMEOUT_MS));
        TEST_ASSERT_TRUE(esp_lv_present_get_frame_count() > before_frames);

        int32_t px = (rot == ESP_DISPLAY_PRESENT_ROTATE_90 || rot == ESP_DISPLAY_PRESENT_ROTATE_180)
                     ? (TEST_LCD_H_RES - 2) : 2;
        int32_t py = (rot == ESP_DISPLAY_PRESENT_ROTATE_180 || rot == ESP_DISPLAY_PRESENT_ROTATE_270)
                     ? (TEST_LCD_V_RES - 2) : 2;
        int32_t opp_x = TEST_LCD_H_RES - 1 - px;
        int32_t opp_y = TEST_LCD_V_RES - 1 - py;
        uint16_t marker_px = s_panel_pixels[py * TEST_LCD_H_RES + px];
        uint16_t bg_px = s_panel_pixels[opp_y * TEST_LCD_H_RES + opp_x];
        TEST_ASSERT_NOT_EQUAL_UINT16(0, marker_px);
        TEST_ASSERT_NOT_EQUAL_UINT16(marker_px, bg_px);

        ESP_LOGI(TAG, "rotation=%u logical=%" PRId32 "x%" PRId32 " frames=%" PRIu32 " marker=0x%04x bg=0x%04x",
                 (unsigned)rot, expected_w, expected_h, esp_lv_present_get_frame_count(), marker_px, bg_px);
    }

    TEST_ESP_OK(esp_display_presenter_quiesce(presenter, TEST_QUIESCE_TIMEOUT_MS));
    TEST_ESP_OK(esp_lv_present_stop());
    test_presenter_delete(presenter);
}

void app_main(void)
{
    printf("ESP LV Present TEST\n");
    unity_run_menu();
}
