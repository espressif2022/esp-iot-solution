/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* S31 TE phase timer diagnostic; output is not optical tear validation. */
#include <inttypes.h>
#include <stdint.h>
#include "board_qspi.h"
#include "esp_display_present.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "te_timer_check";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned timer_calls, timer_ok, timer_errors, full_calls, roi_calls, other_calls, draw_errors;
typedef struct present_te_timer present_te_timer_t;
esp_err_t __real_present_te_timer_arm(present_te_timer_t *timer, uint64_t deadline_us);
esp_err_t __wrap_present_te_timer_arm(present_te_timer_t *timer, uint64_t deadline_us)
{
    esp_err_t error = __real_present_te_timer_arm(timer, deadline_us);
    portENTER_CRITICAL(&lock);
    timer_calls++;
    if (error == ESP_OK) {
        timer_ok++;
    } else {
        timer_errors++;
    }
    portEXIT_CRITICAL(&lock);
    return error;
}
esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0, int y0,
                                           int x1, int y1, const void *color_data);
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x0, int y0,
                                           int x1, int y1, const void *color_data)
{
    esp_err_t error = __real_esp_lcd_panel_draw_bitmap(panel, x0, y0, x1, y1, color_data);
    portENTER_CRITICAL(&lock);
    if (error != ESP_OK) {
        draw_errors++;
    } else if (x0 == 0 && y0 == 0 && x1 == 480 && y1 == 480) {
        full_calls++;
    } else if (x0 == 0 && y0 == 0 && x1 == 64 && y1 == 16) {
        roi_calls++;
    } else {
        other_calls++;
    }
    portEXIT_CRITICAL(&lock);
    return error;
}
static void require_ok(esp_err_t error, const char *operation)
{
    while (error != ESP_OK) {
        ESP_LOGE(TAG, "FAIL operation=%s error=%s; retaining resources", operation, esp_err_to_name(error));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void run_phase(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io, bool roi)
{
    const unsigned width = roi ? 64 : 480, height = roi ? 16 : 480, frames = roi ? 80 : 24;
    require_ok(esp_lcd_panel_set_gap(panel, roi ? 208 : 0, roi ? 232 : 0), "set gap");
    esp_display_presenter_config_t config = {
        .width = width, .height = height, .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .max_damage_areas = 8, .transfer_timeout_ms = 2000, .pipeline_acquire_timeout_ms = 2000,
        .target = {
            .hw = {
                .panel = panel, .io = io, .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .rotation = ESP_DISPLAY_PRESENT_ROTATE_0, .swap_bytes = true,
                .te_enabled = true,
                .te_sync = {.gpio_num = QSPI_TE_GPIO, .bus_freq_hz = 40000000, .data_lines = 4}
            },
            .fb = {.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC},
            .drawbuf = {.lines = 16, .buffers = 2, .te_compose_buffers = 2},
        },
    };
    esp_display_presenter_t *presenter = NULL;
    require_ok(esp_display_presenter_create(&config, &presenter), "presenter create");
    vTaskDelay(pdMS_TO_TICKS(100));
    portENTER_CRITICAL(&lock);
    timer_calls = timer_ok = timer_errors = full_calls = roi_calls = other_calls = draw_errors = 0;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "PHASE_START mode=%s size=%ux%u frames=%u optical_safety=UNVERIFIED", roi ? "ROI" : "FULL", width, height, frames);
    for (unsigned frame = 0; frame < frames; frame++) {
        esp_display_present_surface_request_t request = {0};
        esp_display_present_area_t render[8];
        size_t render_count = 0;
        bool full_coverage = false;
        require_ok(esp_display_presenter_begin_next_frame(presenter, &request, render, 8, &render_count, &full_coverage), "begin");
        for (unsigned y = 0; y < height; y += 16) {
            esp_display_presenter_buffer_t buffer;
            require_ok(esp_display_presenter_acquire_buffer(presenter, &buffer), "acquire");
            require_ok(buffer.capacity_bytes >= width * 16 * 2 ? ESP_OK : ESP_ERR_NO_MEM, "buffer size");
            uint16_t *pixels = buffer.surface.pixels;
            for (unsigned index = 0; index < width * 16; index++) {
                pixels[index] = frame & 1 ? 0x07e0 : 0x001f;
            }
            esp_display_present_area_t area = {0, (int32_t)y, (int32_t)width - 1, (int32_t)y + 15};
            require_ok(esp_display_presenter_submit_buffer(presenter, &buffer, &area, width * 2), "submit");
        }
        esp_display_presenter_submit_t submit = {.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL};
        require_ok(esp_display_presenter_commit_frame(presenter, &submit), "commit");
        require_ok(esp_display_presenter_quiesce(presenter, 5000), "quiesce");
    }
    unsigned arms, ok, arm_errors, full, partial, other, errors;
    portENTER_CRITICAL(&lock);
    arms = timer_calls; ok = timer_ok; arm_errors = timer_errors;
    full = full_calls; partial = roi_calls; other = other_calls; errors = draw_errors;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "PHASE_RESULT mode=%s frames=%u full=%u roi=%u other=%u draw_errors=%u timer_calls=%u timer_ok=%u timer_errors=%u",
             roi ? "ROI" : "FULL", frames, full, partial, other, errors, arms, ok, arm_errors);
    require_ok(!errors && !other && !arm_errors && (roi ? partial == frames && !full && ok > 0 : full == frames && !partial)
               ? ESP_OK : ESP_FAIL, "counts or timer arm");
    require_ok(esp_display_presenter_stop(presenter), "stop");
    require_ok(esp_display_presenter_delete(presenter), "delete");
    ESP_LOGI(TAG, "PHASE_PASS mode=%s", roi ? "ROI" : "FULL");
}
void app_main(void)
{
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    require_ok(qspi_board_create(&panel, &io), "board create");
    run_phase(panel, io, false);
    run_phase(panel, io, true);
    ESP_LOGI(TAG, "TIMER_SUITE_PASS");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
