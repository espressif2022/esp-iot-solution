/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <inttypes.h>
#include "bench_api.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_display_present.h"
#include "esp_lv_present.h"
#include "present_lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static esp_display_presenter_t *presenter;
static lv_display_t *active_display;
static const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC};
esp_err_t bench_init(void)
{
    return hw_lcd_init(&panel, &io, 3, HW_ROTATE_0);
}
esp_err_t bench_open(const bench_case_t *c, lv_display_t **disp)
{
    esp_display_presenter_panel_config_t lcd = {
        .width = HW_LCD_H_RES, .height = HW_LCD_V_RES,
        .hw = {
            .panel = panel, .io = io, .rotation = c->rotation,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
#if BENCH_RGB
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
#elif BENCH_DSI
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI,
#else
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO, .swap_bytes = true,
#endif
#if BENCH_QSPI
            .spi_psram_dma_direct = true, .te_enabled = true,
            .te_sync = {.gpio_num = 43, .bus_freq_hz = 40000000, .data_lines = 4},
#endif
        },
    };
    esp_display_present_mode_t mode = modes[c->mode];
    esp_display_present_drawbuf_config_t tiles = {
        .lines = 50, .buffers = c->tiles, .in_psram = c->psram, .te_compose_buffers = c->compose,
    };
    bool use_fb = mode == ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL || mode == ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL ||
                  mode == ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT;
    esp_err_t e = use_fb ? esp_display_presenter_open(&lcd, mode, &presenter) :
                  esp_display_presenter_open_ex(&lcd, mode, &tiles, &presenter);
    if (e != ESP_OK) {
        return e;
    }
    if ((mode == ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL || mode == ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL) &&
            tiles.buffers == 2) {
        esp_display_presenter_drawbuf_t pool;
        ESP_ERROR_CHECK(esp_display_presenter_get_drawbuf(presenter, &pool));
        assert(pool.count == 2 && pool.lines == tiles.lines && pool.buffers[0] && pool.buffers[1] &&
               pool.buffers[0] != pool.buffers[1]);
        ESP_LOGI("present_bench", "DRAW_POOL mode=%s count=%u lines=%u bytes=%zu first=%p second=%p internal_dma=%u",
                 c->name, (unsigned)pool.count, (unsigned)pool.lines, pool.bytes, pool.buffers[0], pool.buffers[1],
                 (unsigned)(esp_ptr_internal(pool.buffers[0]) && esp_ptr_dma_capable(pool.buffers[0]) &&
                            esp_ptr_internal(pool.buffers[1]) && esp_ptr_dma_capable(pool.buffers[1])));
    }
    e = esp_lv_present_start(presenter, disp);
    active_display = e == ESP_OK ? *disp : NULL;
    if (e == ESP_OK) {
        e = esp_display_presenter_reset_metrics(presenter);
        if (e != ESP_OK) {
            ESP_ERROR_CHECK(esp_lv_present_stop());
            active_display = NULL;
        }
    }
    if (e != ESP_OK) {
        esp_display_presenter_delete(presenter);
        presenter = NULL;
    }
    return e;
}
esp_err_t bench_lock(void)
{
    return ESP_OK;
}
void bench_unlock(void) {}
void bench_pump(void)
{
    uint32_t ms = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(ms == 0 ? 1 : ms > 10 ? 10 : ms));
}
esp_err_t bench_close(void)
{
    esp_err_t e = esp_display_presenter_quiesce(presenter, 10000);
    if (e != ESP_OK) {
        return e;
    }
    /* A binding error can pause drawing while benchmark timers still finish.
     * Check it outside the measured scene interval; cleanup remains mandatory. */
    esp_err_t draw_error = esp_lv_present_native_get_error(lv_display_get_user_data(active_display));
    e = esp_lv_present_stop();
    if (e != ESP_OK) {
        return e;
    }
    active_display = NULL;
    e = esp_display_presenter_delete(presenter);
    return e != ESP_OK ? e : draw_error;
}
uint32_t bench_frame_count(void)
{
    return esp_lv_present_get_frame_count();
}
void bench_metrics_report(const bench_case_t *test)
{
    esp_display_presenter_metrics_t metrics;
    ESP_ERROR_CHECK(esp_display_presenter_get_metrics(presenter, &metrics));
    ESP_LOGI("present_bench", "METRICS mode=%s te_buffers=%u rotation=%u "
             "frame_samples=%" PRIu64 " begin_commit_p99_us=%" PRIu64
             " buffer_waits=%" PRIu64 " buffer_wait_us=%" PRIu64 " buffer_wait_max_us=%" PRIu64
             " buffer_wait_timeouts=%" PRIu64 " scanout_available=%u"
             " scanout_vsyncs=%" PRIu64 " scanout_supply_misses=%" PRIu64,
             test->name, test->compose, test->rotation,
             metrics.frame_samples, metrics.frame_p99_us, metrics.buffer_waits, metrics.buffer_wait_us,
             metrics.buffer_wait_max_us, metrics.buffer_wait_timeouts, (unsigned)metrics.scanout_available,
             metrics.scanout_vsyncs, metrics.scanout_supply_misses);
}
#if BENCH_QSPI
void meter_reset(void); void meter_report(const char *);
void bench_meter_reset(void)
{
    meter_reset();
}
void bench_meter_report(const char *label)
{
    meter_report(label);
}
#else
void bench_meter_reset(void) {}
void bench_meter_report(const char *label)
{
    (void)label;
}
#endif
