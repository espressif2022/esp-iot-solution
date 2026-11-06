/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include <stdatomic.h>
#include "bench_api.h"
#include "bench_cases.h"
#include "demos/lv_demos.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "present_bench";
static atomic_bool finished;
static const bench_case_t *active;
static int fps, cpu, render, flush, scenes;
static void end_cb(const lv_demo_benchmark_summary_t *s)
{
    scenes = s->valid_scene_cnt;
    fps = scenes ? s->total_avg_fps / scenes : 0;
    cpu = scenes ? s->total_avg_cpu / scenes : 0;
    render = scenes ? s->total_avg_render_time / scenes : 0;
    flush = scenes ? s->total_avg_flush_time / scenes : 0;
    for (lv_demo_benchmark_scene_dsc_t *p = s->scenes; p->create_cb; p++) {
        if (p->measurement_cnt) {
            ESP_LOGI(TAG, "SCENE mode=%s rotation=%u name=\"%s\" fps=%u cpu=%u%% render=%ums flush=%ums", active->name, active->rotation, p->name, (unsigned)(p->fps_avg / p->measurement_cnt), (unsigned)(p->cpu_avg_usage / p->measurement_cnt), (unsigned)(p->render_avg_time / p->measurement_cnt), (unsigned)(p->flush_avg_time / p->measurement_cnt));
        }
        p->cpu_avg_usage = p->fps_avg = p->render_avg_time = p->flush_avg_time = p->measurement_cnt = 0;
    }
    atomic_store_explicit(&finished, true, memory_order_release);
}
static void run(void *unused)
{
    (void)unused;
    ESP_ERROR_CHECK(bench_init());
    ESP_LOGI(TAG, "SUITE_BEGIN board=%s implementation=%s cases=%u", BENCH_BOARD, BENCH_IMPLEMENTATION, (unsigned)(sizeof(bench_cases) / sizeof(bench_cases[0])));
    for (unsigned i = 0; i < sizeof(bench_cases) / sizeof(bench_cases[0]); i++) {
        active = &bench_cases[i];
#ifdef BENCH_ADAPTER
        if (!active->adapter_supported) {
            ESP_LOGI(TAG, "SKIP mode=%s rotation=%u reason=%s", active->name, active->rotation, active->skip_reason);
            continue;
        }
#endif
        lv_display_t *disp = NULL;
        ESP_ERROR_CHECK(bench_open(active, &disp));
        atomic_store(&finished, false);
        bench_meter_reset();
        ESP_ERROR_CHECK(bench_lock());
        lv_demo_benchmark_set_end_cb(end_cb);
        lv_demo_benchmark();
        lv_obj_invalidate(lv_display_get_screen_active(disp));
        bench_unlock();
        ESP_LOGI(TAG, "RUN mode=%s te_buffers=%u rotation=%u", active->name, active->compose, active->rotation);
        int64_t start = esp_timer_get_time();
        while (!atomic_load_explicit(&finished, memory_order_acquire) && esp_timer_get_time() - start < 150000000) {
            bench_pump();
        }
        bool complete = atomic_load_explicit(&finished, memory_order_acquire);
        int64_t elapsed = esp_timer_get_time() - start;
        uint32_t frames = bench_frame_count();
        ESP_ERROR_CHECK(bench_close());
        bench_meter_report(active->name);
        ESP_ERROR_CHECK(complete && scenes > 0 ? ESP_OK : ESP_ERR_TIMEOUT);
        ESP_LOGI(TAG, "PASS mode=%s te_buffers=%u rotation=%u frames=%u transport_fps=%.1f lvgl_fps=%d cpu=%d%% render=%dms flush=%dms scenes=%d", active->name, active->compose, active->rotation, (unsigned)frames, frames * 1000000.0 / elapsed, fps, cpu, render, flush, scenes);
    }
    ESP_LOGI(TAG, "SUITE_PASS board=%s implementation=%s", BENCH_BOARD, BENCH_IMPLEMENTATION);
    vTaskDelete(NULL);
}
void app_main(void)
{
    ESP_ERROR_CHECK(xTaskCreate(run, "benchmark", 24576, NULL, 6, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
