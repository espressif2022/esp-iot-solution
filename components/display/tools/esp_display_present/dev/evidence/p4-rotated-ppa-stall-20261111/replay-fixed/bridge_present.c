/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "bench_api.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "pixel/present_executor.h"
#include "esp_log.h"
#include "core/present_engine.h"
extern void present_test_engine_dump(esp_display_present_engine_t *);
#include "esp_display_present.h"
#include "esp_lv_present.h"
#include "present_lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern esp_display_present_engine_t *present_test_get_engine(esp_display_presenter_t *);
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static esp_display_presenter_t *presenter;
static lv_display_t *active_display;
static const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC};
static volatile unsigned replay_iteration;
extern void present_test_ppa_dump(void);
static void replay_watch(void *unused) { vTaskDelay(pdMS_TO_TICKS(2000)); ESP_LOGW("PPA_REPLAY", "WATCH iteration=%u",replay_iteration); present_test_ppa_dump(); vTaskDelete(NULL); }
esp_err_t bench_init(void)
{
    ESP_ERROR_CHECK(hw_lcd_init(&panel, &io, 3, HW_ROTATE_0));
    void *fbs[3];
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel,3,&fbs[0],&fbs[1],&fbs[2]));
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,1024,600,fbs[2]));
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t *src=heap_caps_aligned_alloc(64,60032,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    assert(src);
    for(unsigned i=0;i<60000;i++) src[i]=(uint8_t)(i*37+i/128);
    esp_display_present_executor_t *ppa;
    ESP_ERROR_CHECK(esp_display_present_executor_ppa_create(&ppa));
    esp_display_present_copy_request_t req={
      .src={src,60000,{128,130,256,ESP_DISPLAY_PRESENT_FORMAT_RGB565,ESP_DISPLAY_PRESENT_ORDER_NATIVE}},
      .dst={fbs[0],1228800,{1024,600,2048,ESP_DISPLAY_PRESENT_FORMAT_RGB565,ESP_DISPLAY_PRESENT_ORDER_NATIVE}},
      .src_area={0,0,128,130}, .dst_x=727,.dst_y=353,.rotation=ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270
    };
    esp_display_present_executor_t *cpu;
    ESP_ERROR_CHECK(esp_display_present_executor_cpu_create(&cpu));
    esp_display_present_executor_dispatch_t dispatch={ppa,cpu};
    memset(fbs[0],0xa5,1228800); memset(fbs[1],0xa5,1228800);
    const unsigned heights[]={129,131,130};
    xTaskCreate(replay_watch,"replay_watch",4096,NULL,5,NULL);
    for(unsigned j=0;j<20000;j++) {
      replay_iteration=j; unsigned i=j%3;
      req.src_area.height=req.src.layout.height=heights[i];
      if(j%1000==0) ESP_LOGW("PPA_REPLAY","BEGIN iteration=%u width=128 height=%u rotation=270",j,heights[i]);
      esp_display_present_executor_selection_t selected;
      esp_err_t e=esp_display_present_executor_run(&dispatch,&req,&selected);
      ESP_ERROR_CHECK(e);
      if(e!=ESP_OK) ESP_LOGW("PPA_REPLAY","ERROR iteration=%u result=%s",j,esp_err_to_name(e));
    }
    req.dst.pixels=fbs[1];
    ESP_ERROR_CHECK(esp_display_present_executor_execute(cpu,&req));
    assert(memcmp(fbs[0],fbs[1],1228800)==0);
    ESP_LOGW("PPA_REPLAY","ALL_RETURNED iterations=20000 pixels_match=1");
    for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
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
    ESP_LOGW("CLOSE_STATE", "before binding=%s", esp_err_to_name(esp_lv_present_native_get_error(lv_display_get_user_data(active_display))));
    esp_err_t e = esp_display_presenter_quiesce(presenter, 10000);
    ESP_LOGW("CLOSE_STATE", "quiesce=%s", esp_err_to_name(e));
    if (e != ESP_OK) present_test_engine_dump(present_test_get_engine(presenter));
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
