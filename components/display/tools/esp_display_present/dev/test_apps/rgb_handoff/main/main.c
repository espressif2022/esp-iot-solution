/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
/* Real presenter, public driver callbacks. Wrappers only widen the submit
 * window and observe pixels/events; no production or IDF instrumentation. */
#include <inttypes.h>
#include <string.h>
#include "board.h"
#include "esp_display_present.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define FRAMES 4
static const char *TAG = "rgb_handoff";
static portMUX_TYPE meter_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    esp_lcd_rgb_panel_frame_buf_complete_cb_t original;
    uint32_t events, early;
    bool drawing;
    unsigned delay_ms, core;
    const uint16_t *pixels;
} meter;
static esp_lcd_panel_handle_t panel;
static TaskHandle_t controller;
static const struct {
    const char *name;
    esp_display_present_mode_t mode;
} profiles[] = {
    {"DOUBLE_FULL", ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL},
    {"TRIPLE_FULL", ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL},
    {"DOUBLE_DIRECT", ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT},
    {"DOUBLE_PARTIAL", ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL},
    {"TRIPLE_PARTIAL", ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL},
    {"AUTO", ESP_DISPLAY_PRESENT_MODE_AUTO},
};
static const unsigned rotations[] = {0, 90, 180, 270};

static bool IRAM_ATTR observed_complete(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_data_t *event, void *user)
{
    portENTER_CRITICAL_ISR(&meter_lock);
    esp_lcd_rgb_panel_frame_buf_complete_cb_t callback = meter.original;
    ++meter.events;
    meter.early += meter.drawing;
    portEXIT_CRITICAL_ISR(&meter_lock);
    return callback && user ? callback(p, event, user) : false;
}
esp_err_t __real_esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_callbacks_t *, void *);
esp_err_t __wrap_esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_callbacks_t *callbacks, void *user)
{
    esp_lcd_rgb_panel_event_callbacks_t copy = *callbacks;
    portENTER_CRITICAL(&meter_lock);
    meter.original = callbacks->on_frame_buf_complete;
    portEXIT_CRITICAL(&meter_lock);
    if (copy.on_frame_buf_complete) {
        copy.on_frame_buf_complete = observed_complete;
    }
    return __real_esp_lcd_rgb_panel_register_event_callbacks(p, &copy, user);
}
esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p, int x0, int y0, int x1, int y1, const void *pixels)
{
    ESP_ERROR_CHECK(x0 == 0 && y0 == 0 && x1 == WIDTH && y1 == HEIGHT && pixels ? ESP_OK : ESP_ERR_INVALID_SIZE);
    portENTER_CRITICAL(&meter_lock);
    meter.pixels = pixels;
    meter.drawing = true;
    unsigned delay = meter.delay_ms;
    portEXIT_CRITICAL(&meter_lock);
    esp_err_t result = __real_esp_lcd_panel_draw_bitmap(p, x0, y0, x1, y1, pixels);
    if (result == ESP_OK && delay) {
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
    portENTER_CRITICAL(&meter_lock);
    meter.drawing = false;
    portEXIT_CRITICAL(&meter_lock);
    return result;
}
static uint16_t pattern(unsigned x, unsigned y, unsigned w, unsigned h, unsigned frame)
{
    static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffe0, 0xf81f, 0x07ff};
    if (x < 3 || y < 3 || x >= w - 3 || y >= h - 3 ||
            (x >= 30 + frame * 17 && x < 62 + frame * 17 && y >= h / 2 && y < h / 2 + 32)) {
        return 0xffff;
    }
    return colors[(x * 6 / w + frame) % 6];
}
static void run_case(unsigned index, unsigned rotation)
{
    unsigned w = rotation % 180 ? HEIGHT : WIDTH;
    unsigned h = rotation % 180 ? WIDTH : HEIGHT;
    esp_display_presenter_config_t config = {
        .width = w, .height = h, .max_damage_areas = 8,
        .transfer_timeout_ms = 3000, .pipeline_acquire_timeout_ms = 3000,
        .target = {
            .hw = {
                .panel = panel, .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB, .rotation = rotation,
                .rgb = {
                    .scanout =
#ifdef PRESENT_BOARD_S31_RGB
                    ESP_DISPLAY_PRESENT_RGB_SCANOUT_LINK_SWITCH
#else
                    ESP_DISPLAY_PRESENT_RGB_SCANOUT_BOUNCE
#endif
                }
            },
            .fb = {.mode = profiles[index].mode},
            .drawbuf = {.lines = 50, .buffers = 1},
        },
    };
    ESP_LOGI(TAG, "BEGIN mode=%s_C%u_D%u rotation=%u", profiles[index].name, meter.core, meter.delay_ms, rotation);
    portENTER_CRITICAL(&meter_lock);
    uint32_t events = meter.events, early = meter.early;
    portEXIT_CRITICAL(&meter_lock);
    esp_display_presenter_t *presenter = NULL;
    ESP_ERROR_CHECK(esp_display_presenter_create(&config, &presenter));
    esp_display_presenter_caps_t caps;
    ESP_ERROR_CHECK(esp_display_presenter_get_caps(presenter, &caps));
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        size_t n;
        bool full;
        ESP_ERROR_CHECK(esp_display_presenter_begin_next_frame(presenter, NULL, NULL, 0, &n, &full));
        for (unsigned y = 0; y < h;) {
            esp_display_presenter_buffer_t buffer;
            ESP_ERROR_CHECK(esp_display_presenter_acquire_buffer(presenter, &buffer));
            size_t rows = h - y;
            if (caps.contract == ESP_DISPLAY_PRESENT_CONTRACT_PARTITION) {
                ESP_ERROR_CHECK(buffer.resolve_rows(buffer.resolve_rows_ctx, buffer.lease_id, w * 2, rows, &rows));
            }
            ESP_ERROR_CHECK(rows && rows <= h - y && buffer.capacity_bytes >= w * rows * 2 ? ESP_OK : ESP_ERR_INVALID_SIZE);
            uint16_t *pixels = buffer.surface.pixels;
            for (unsigned yy = 0; yy < rows; ++yy)
                for (unsigned x = 0; x < w; ++x) {
                    pixels[yy * w + x] = pattern(x, y + yy, w, h, frame);
                }
            esp_display_present_area_t area = {0, y, w - 1, y + rows - 1};
            ESP_ERROR_CHECK(esp_display_presenter_submit_buffer(presenter, &buffer, &area, w * 2));
            y += rows;
        }
        esp_display_presenter_submit_t submit = {.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL};
        ESP_ERROR_CHECK(esp_display_presenter_commit_frame(presenter, &submit));
        ESP_ERROR_CHECK(esp_display_presenter_quiesce(presenter, 3000));
        portENTER_CRITICAL(&meter_lock);
        const uint16_t *pixels = meter.pixels;
        portEXIT_CRITICAL(&meter_lock);
        ESP_ERROR_CHECK(pixels ? ESP_OK : ESP_FAIL);
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                unsigned px, py;
                switch (rotation) {
                case 90: px = h - 1 - y; py = x; break;
                case 180: px = w - 1 - x; py = h - 1 - y; break;
                case 270: px = y; py = w - 1 - x; break;
                default: px = x; py = y; break;
                }
                ESP_ERROR_CHECK(pixels[py * WIDTH + px] == pattern(x, y, w, h, frame) ? ESP_OK : ESP_FAIL);
            }
    }
    ESP_ERROR_CHECK(esp_display_presenter_delete(presenter));
    portENTER_CRITICAL(&meter_lock);
    events = meter.events - events;
    early = meter.early - early;
    portEXIT_CRITICAL(&meter_lock);
    ESP_ERROR_CHECK(events >= FRAMES && (!meter.delay_ms || early) ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "PASS mode=%s_C%u_D%u rotation=%u frames=%u driver_events=%"PRIu32" early_events=%"PRIu32" pixels=ALL",
             profiles[index].name, meter.core, meter.delay_ms, rotation, FRAMES, events, early);
}
static void worker(void *unused)
{
    (void)unused;
    for (unsigned delay = 0; delay < 2; ++delay) {
        portENTER_CRITICAL(&meter_lock);
        meter.delay_ms = delay ? 50 : 0;
        portEXIT_CRITICAL(&meter_lock);
        for (unsigned profile = 0; profile < sizeof(profiles) / sizeof(profiles[0]); ++profile)
            for (unsigned angle = 0; angle < sizeof(rotations) / sizeof(rotations[0]); ++angle) {
                run_case(profile, rotations[angle]);
            }
    }
    xTaskNotifyGive(controller);
    vTaskDelete(NULL);
}
void app_main(void)
{
    controller = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(board_panel_create(&panel, 3));
    for (unsigned core = 0; core < 2; ++core) {
        meter.core = core;
        ESP_ERROR_CHECK(xTaskCreatePinnedToCore(worker, "rgb_handoff", 16384, NULL, 6, NULL, core) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_del(panel));
    ESP_ERROR_CHECK(board_resources_destroy());
    ESP_LOGI(TAG, "RGB_HANDOFF_SUITE_PASS board=%s cases=96 frames_per_case=%u", BOARD_NAME, FRAMES);
}
