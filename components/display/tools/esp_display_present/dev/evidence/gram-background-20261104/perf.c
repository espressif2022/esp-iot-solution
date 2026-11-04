/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 * Matched V1/current single-producer API phase timing. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_display_present.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_IDF_TARGET_ESP32S31
#include "board_qspi.h"
#define WIDTH 480
#define HEIGHT 480
#define PANEL_KIND "S31_QSPI"
#else
#include "board.h"
#define PANEL_KIND BOARD_NAME
#endif
#ifndef TEST_IMPL
#define TEST_IMPL "unknown"
#endif
#define WARMUP 8
#define FRAMES 60
#define REPEATS 3
static const char *TAG = "phase_perf";

typedef struct {
    const char *name;
    esp_display_present_mode_t mode;
    unsigned compose, rotation;
    bool te;
    unsigned tiles;
    bool psram;
} profile_t;

#if CONFIG_IDF_TARGET_ESP32S31
static const profile_t profiles[] = {
    {"GRAM_T1_0", ESP_DISPLAY_PRESENT_MODE_NONE, 0, 0, false, 1, false},
    {"GRAM_T2_0", ESP_DISPLAY_PRESENT_MODE_NONE, 0, 0, false, 2, false},
    {"GRAM_T2_90", ESP_DISPLAY_PRESENT_MODE_NONE, 0, 90, false, 2, false},
    {"GRAM_T2_PSRAM", ESP_DISPLAY_PRESENT_MODE_NONE, 0, 0, false, 2, true},
};
#else
static const profile_t profiles[] = {
    {"DOUBLE_PARTIAL", ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, 0, 0, false},
    {"TRIPLE_PARTIAL", ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, 0, 0, false},
    {"DOUBLE_DIRECT", ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, 0, 0, false},
    {"TRIPLE_FULL", ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, 0, 0, false},
};
#endif

typedef struct {
    uint64_t begin, caps, acquire, rows, render, submit, commit, drain, total;
    uint64_t max_frame, max_submit, max_acquire;
} timings_t;

static void require(esp_err_t e, const char *step)
{
    while (e != ESP_OK) {
        ESP_LOGE(TAG, "FATAL impl=%s step=%s error=%s", TEST_IMPL, step, esp_err_to_name(e));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static inline uint64_t clock_us(void) { return (uint64_t)esp_timer_get_time(); }

static void draw_frame(esp_display_presenter_t *presenter, uint32_t frame, timings_t *timing)
{
    uint64_t frame_start = clock_us(), start;
    size_t count;
    bool full;
    start = clock_us();
    require(esp_display_presenter_begin_next_frame(presenter, NULL, NULL, 0, &count, &full), "begin");
    timing->begin += clock_us() - start;
    esp_display_presenter_caps_t caps;
    start = clock_us();
    require(esp_display_presenter_get_caps(presenter, &caps), "caps");
    timing->caps += clock_us() - start;
    require(caps.width <= WIDTH && caps.height <= HEIGHT ? ESP_OK : ESP_ERR_INVALID_SIZE, "dimensions");
    uint16_t row[WIDTH];
    start = clock_us();
    for (uint32_t x = 0; x < caps.width; ++x) {
        uint16_t c = x < caps.width / 3 ? 0xf800 : x < caps.width * 2 / 3 ? 0x07e0 : 0x001f;
        if (x >= (frame * 13) % caps.width && x < (frame * 13) % caps.width + 12) c = 0xffff;
        row[x] = c;
    }
    timing->render += clock_us() - start;
    for (uint32_t y = 0; y < caps.height;) {
        esp_display_presenter_buffer_t buffer;
        start = clock_us();
        require(esp_display_presenter_acquire_buffer(presenter, &buffer), "acquire");
        uint64_t waited = clock_us() - start;
        timing->acquire += waited;
        if (waited > timing->max_acquire) timing->max_acquire = waited;
        size_t rows = caps.height - y;
        start = clock_us();
        if (caps.contract == ESP_DISPLAY_PRESENT_CONTRACT_PARTITION) {
            require(buffer.resolve_rows(buffer.resolve_rows_ctx, buffer.lease_id, caps.width * 2, rows, &rows), "rows");
        }
        require(rows > 0 && rows <= caps.height - y && buffer.capacity_bytes >= caps.width * rows * 2 ? ESP_OK : ESP_ERR_INVALID_SIZE, "buffer");
        timing->rows += clock_us() - start;
        start = clock_us();
        for (size_t yy = 0; yy < rows; ++yy) {
            memcpy((uint16_t *)buffer.surface.pixels + yy * caps.width, row, caps.width * 2);
        }
        timing->render += clock_us() - start;
        esp_display_present_area_t area = {0, y, caps.width - 1, y + rows - 1};
        start = clock_us();
        require(esp_display_presenter_submit_buffer(presenter, &buffer, &area, caps.width * 2), "submit");
        waited = clock_us() - start;
        timing->submit += waited;
        if (waited > timing->max_submit) timing->max_submit = waited;
        y += rows;
    }
    esp_display_presenter_submit_t submit = {.coverage = ESP_DISPLAY_PRESENT_COVERAGE_FULL};
    start = clock_us();
    require(esp_display_presenter_commit_frame(presenter, &submit), "commit");
    timing->commit += clock_us() - start;
    uint64_t elapsed = clock_us() - frame_start;
    if (elapsed > timing->max_frame) timing->max_frame = elapsed;
}

static void drain(esp_display_presenter_t *presenter, timings_t *timing)
{
    uint64_t start = clock_us();
    require(esp_display_presenter_quiesce(presenter, 10000), "quiesce");
    esp_display_presenter_frame_status_t status;
    for (;;) {
        require(esp_display_presenter_get_frame_status(presenter, &status), "status");
        if (status.completed_present_frame >= status.last_submitted_frame) break;
        require(clock_us() - start < 10000000 ? ESP_OK : ESP_ERR_TIMEOUT, "final present watermark");
        vTaskDelay(1);
    }
    timing->drain += clock_us() - start;
}

static void run_case(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io, const profile_t *p)
{
    esp_display_presenter_config_t config = {
        .width = WIDTH, .height = HEIGHT, .max_damage_areas = 32,
        .transfer_timeout_ms = 10000, .pipeline_acquire_timeout_ms = 10000,
        .target = {.hw = {.panel = panel, .io = io, .rotation = p->rotation,
#if CONFIG_IDF_TARGET_ESP32S31
                          .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO, .swap_bytes = true,
                          .te_enabled = p->te, .te_sync = {.gpio_num = QSPI_TE_GPIO, .bus_freq_hz = 40000000, .data_lines = 4},
#elif CONFIG_IDF_TARGET_ESP32S3
                          .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
#else
                          .panel_type = ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI,
#endif
                         }, .fb = {.mode = p->mode},
                   .drawbuf = {.lines = 16, .buffers = p->tiles, .in_psram = p->psram, .te_compose_buffers = p->compose}}
    };
    esp_display_presenter_t *presenter = NULL;
    vTaskDelay(1);
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    require(esp_display_presenter_create(&config, &presenter), "create");
    size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "MEMORY impl=%s profile=%s internal_bytes=%zu", TEST_IMPL, p->name, before - after);
    ESP_LOGI(TAG, "CASE_BEGIN impl=%s board=%s profile=%s", TEST_IMPL, PANEL_KIND, p->name);
    timings_t warm = {0};
    for (unsigned i = 0; i < WARMUP; ++i) draw_frame(presenter, i, &warm);
    drain(presenter, &warm);
    for (unsigned rep = 0; rep < REPEATS; ++rep) {
        timings_t t = {0};
        uint64_t start = clock_us();
        for (unsigned i = 0; i < FRAMES; ++i) draw_frame(presenter, i + WARMUP + rep * FRAMES, &t);
        drain(presenter, &t);
        t.total = clock_us() - start;
        ESP_LOGI(TAG, "SAMPLE impl=%s board=%s profile=%s rep=%u frames=%u total_us=%"PRIu64
                 " begin_us=%"PRIu64" caps_us=%"PRIu64" acquire_us=%"PRIu64" rows_us=%"PRIu64
                 " render_us=%"PRIu64" submit_us=%"PRIu64" commit_us=%"PRIu64" drain_us=%"PRIu64" max_frame_us=%"PRIu64" max_submit_us=%"PRIu64" max_acquire_us=%"PRIu64,
                 TEST_IMPL, PANEL_KIND, p->name, rep, FRAMES, t.total, t.begin, t.caps, t.acquire, t.rows,
                 t.render, t.submit, t.commit, t.drain, t.max_frame, t.max_submit, t.max_acquire);
    }
    require(esp_display_presenter_stop(presenter), "stop");
    require(esp_display_presenter_delete(presenter), "delete");
}
static void run(void *unused)
{
    (void)unused;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    vTaskDelay(pdMS_TO_TICKS(3000));
#if CONFIG_IDF_TARGET_ESP32S31
    require(qspi_board_create(&panel, &io), "board");
    require(esp_lcd_panel_set_gap(panel, 0, 0), "gap");
#else
    require(board_panel_create(&panel, 3), "board");
#endif
    for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); ++i) run_case(panel, io, &profiles[i]);
    ESP_LOGI(TAG, "PERF_DONE impl=%s board=%s cases=%u repeats=%u frames=%u", TEST_IMPL, PANEL_KIND,
             (unsigned)(sizeof(profiles) / sizeof(profiles[0])), REPEATS, FRAMES);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
void app_main(void)
{
    if (xTaskCreate(run, "phase_perf", 16384, NULL, 6, NULL) != pdPASS) require(ESP_ERR_NO_MEM, "task");
}
