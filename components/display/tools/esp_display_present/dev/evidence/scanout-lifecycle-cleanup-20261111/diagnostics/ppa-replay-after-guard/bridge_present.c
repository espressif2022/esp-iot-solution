/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "bench_api.h"
#include "esp_log.h"
#include "core/present_engine.h"
#include "pixel/present_executor.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_display_present.h"
#include "esp_lv_present.h"
#include "present_lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern esp_display_present_engine_t *present_test_get_engine(esp_display_presenter_t *);
extern esp_err_t present_test_engine_snapshot(esp_display_present_engine_t *, esp_display_present_engine_status_t *);
extern void present_test_engine_dump(esp_display_present_engine_t *);
extern esp_err_t present_test_binding_error(esp_lv_present_native_t *);
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static esp_display_presenter_t *presenter;
static lv_display_t *active_display;
static void close_state(const char *phase)
{
    esp_display_present_engine_status_t s = {0};
    esp_err_t e = present_test_engine_snapshot(present_test_get_engine(presenter), &s);
    ESP_LOGI("CLOSE_STATE", "%s result=%s active=%d cancel=%d pending=%u committed=%llu completed=%llu first=%s service=%s binding=%s", phase, esp_err_to_name(e), s.frame_active, s.cancel_pending, (unsigned)s.pending_frames, (unsigned long long)s.committed_frames, (unsigned long long)s.completed_frames, esp_err_to_name(s.first_error), esp_err_to_name(s.last_service_error), esp_err_to_name(present_test_binding_error(lv_display_get_user_data(active_display))));
}


static const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC};
esp_err_t bench_init(void)
{
    ESP_ERROR_CHECK(hw_lcd_init(&panel, &io, 3, HW_ROTATE_0));
    void *fbs[3];
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel,3,&fbs[0],&fbs[1],&fbs[2]));
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,1024,600,fbs[2]));
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_display_present_executor_t *ppa;
    ESP_ERROR_CHECK(esp_display_present_executor_ppa_create(&ppa));
    esp_display_present_layout_t layout={.width=1024,.height=600,.stride_bytes=2048,.format=ESP_DISPLAY_PRESENT_FORMAT_RGB565,.byte_order=ESP_DISPLAY_PRESENT_ORDER_NATIVE};
    esp_display_present_copy_request_t request={.src={fbs[1],1228800,layout},.dst={fbs[0],1228800,layout},.src_area={466,248,130,352},.dst_x=466,.dst_y=248};
    esp_display_present_executor_t *cpu;
    ESP_ERROR_CHECK(esp_display_present_executor_cpu_create(&cpu));
    esp_display_present_executor_dispatch_t dispatch={ppa,cpu};
    const unsigned widths[]={130,132,131};
    for (unsigned i=0;i<3;++i) {
        request.src_area.width=widths[i];
        ESP_LOGI("PPA_REPLAY", "BEGIN width=%u height=352 x=466 y=248",widths[i]);
        esp_display_present_executor_selection_t selected;
        esp_err_t e=esp_display_present_executor_run(&dispatch,&request,&selected);
        ESP_LOGI("PPA_REPLAY","selected=%s fallback=%d primary_reason=%d",selected.executor==cpu?"cpu":"ppa",selected.used_fallback,selected.primary_reason);
        ESP_LOGI("PPA_REPLAY", "END width=%u result=%s",widths[i],esp_err_to_name(e));
    }
    ESP_LOGI("PPA_REPLAY","ALL_RETURNED");
    vTaskDelete(NULL);
    return ESP_OK;
}
esp_err_t bench_open(const bench_case_t *c, lv_display_t **disp)
{
    esp_display_presenter_config_t cfg = {.width = HW_LCD_H_RES, .height = HW_LCD_V_RES, .max_damage_areas = 32,
                                          .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                                          .target = {.hw = {.panel = panel, .io = io, .rotation = c->rotation,
#if BENCH_RGB
                                                            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
#elif BENCH_DSI
                                                            .panel_type = ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI,
#else
                                                            .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO, .swap_bytes = true,
#endif
#if BENCH_QSPI
                                                            .spi_psram_dma_direct = true, .te_enabled = true, .te_sync = {.gpio_num = 43, .bus_freq_hz = 40000000, .data_lines = 4},
#endif
                                                           }, .fb = {.mode = modes[c->mode]}, .drawbuf = {.lines = 50, .buffers = c->tiles, .in_psram = c->psram, .te_compose_buffers = c->compose}
                                                    }
                                         };
    if (c->rotation == 90 || c->rotation == 270) {
        cfg.width = HW_LCD_V_RES;
        cfg.height = HW_LCD_H_RES;
    }
    esp_err_t e = esp_display_presenter_create(&cfg, &presenter);
    if (e != ESP_OK) {
        return e;
    }
    e = esp_lv_present_start(presenter, disp);
    active_display = e == ESP_OK ? *disp : NULL;
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
    close_state("before");
    esp_err_t e = esp_display_presenter_quiesce(presenter, 10000);
    close_state("after");
    if (e != ESP_OK) present_test_engine_dump(present_test_get_engine(presenter));
    ESP_LOGI("CLOSE_TRACE", "quiesce=%s", esp_err_to_name(e));
    if (e != ESP_OK) {
        return e;
    }
    /* A binding error can pause drawing while benchmark timers still finish.
     * Check it outside the measured scene interval; cleanup remains mandatory. */
    esp_err_t draw_error = esp_lv_present_native_get_error(lv_display_get_user_data(active_display));
    ESP_LOGI("CLOSE_TRACE", "draw_error=%s", esp_err_to_name(draw_error));
    e = esp_lv_present_stop();
    ESP_LOGI("CLOSE_TRACE", "binding_stop=%s", esp_err_to_name(e));
    if (e != ESP_OK) {
        return e;
    }
    active_display = NULL;
    e = esp_display_presenter_delete(presenter);
    ESP_LOGI("CLOSE_TRACE", "presenter_delete=%s", esp_err_to_name(e));
    return e != ESP_OK ? e : draw_error;
}
uint32_t bench_frame_count(void)
{
    return esp_lv_present_get_frame_count();
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
