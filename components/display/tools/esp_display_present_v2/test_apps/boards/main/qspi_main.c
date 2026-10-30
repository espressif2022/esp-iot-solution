/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <inttypes.h>
#include "board_qspi.h"
#include "present_presenter.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static pv2_presenter_t *te_presenter;
static uint64_t last_rise, period_min = UINT64_MAX, period_max, high_min = UINT64_MAX;
static unsigned edges;
static const char *TAG = "qspi_test";
static void check(esp_err_t e, const char *what)
{
    while (e != ESP_OK) {
        ESP_LOGE(TAG, "FAIL %s: %s; objects retained", what, esp_err_to_name(e));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void te_isr(void *unused)
{
    (void)unused;
    uint64_t now = esp_timer_get_time();
    bool yield = false;
    portENTER_CRITICAL_ISR(&mux);
    if (gpio_get_level(QSPI_TE_GPIO)) {
        if (last_rise) {
            uint64_t delta = now - last_rise;
            if (delta < period_min) {
                period_min = delta;
            }
            if (delta > period_max) {
                period_max = delta;
            }
        }
        last_rise = now;
        edges++;
        if (te_presenter) {
            yield = pv2_presenter_notify_te_isr(te_presenter, now);
        }
    } else if (last_rise && now - last_rise < high_min) {
        high_min = now - last_rise;
    }
    portEXIT_CRITICAL_ISR(&mux);
    if (yield) {
        portYIELD_FROM_ISR();
    }
}

typedef struct {
    pv2_panel_adapter_t real;
    pv2_panel_event_fn callback;
    void *user;
    uint64_t started, completed, max_tx, max_edge_age;
    unsigned offered, done, overruns;
    uint64_t edge_at_submit;
    uint32_t window;
} meter_t;
static bool metered_done(void *ctx, const pv2_backend_event_t *e, bool isr)
{
    meter_t *m = ctx;
    uint64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&mux);
    m->completed = now;
    uint64_t us = now - m->started;
    if (us > m->max_tx) {
        m->max_tx = us;
    }
    if (m->window && now - m->edge_at_submit > m->window) {
        m->overruns++;
    }
    m->done++;
    portEXIT_CRITICAL_ISR(&mux);
    return m->callback(m->user, e, isr);
}
static esp_err_t metered_attach(void *ctx, pv2_panel_event_fn callback, void *user)
{
    meter_t *m = ctx;
    m->callback = callback;
    m->user = user;
    return m->real.attach(m->real.context, metered_done, m);
}
static pv2_tx_result_t metered_submit(void *ctx, const pv2_surface_t *s, const pv2_rect_t *area, uint64_t cookie)
{
    meter_t *m = ctx;
    portENTER_CRITICAL(&mux);
    uint64_t now = esp_timer_get_time();
    uint64_t rise = last_rise;
    m->started = now;
    m->edge_at_submit = rise;
    portEXIT_CRITICAL(&mux);
    pv2_tx_result_t res = m->real.submit(m->real.context, s, area, cookie);
    if (res.acceptance != PV2_TX_REJECTED) {
        portENTER_CRITICAL(&mux);
        uint64_t age = rise ? now - rise : 0;
        if (age > m->max_edge_age) {
            m->max_edge_age = age;
        }
        m->offered++;
        portEXIT_CRITICAL(&mux);
    }
    return res;
}
static esp_err_t metered_stop(void *ctx, uint32_t timeout)
{
    meter_t *m = ctx;
    return m->real.quiesce(m->real.context, timeout);
}
static const uint16_t colors[8] = {0xf800, 0x07e0, 0x001f, 0xffff, 0, 0xffe0, 0x07ff, 0xf81f};

/* Full-screen GRAM uses bands; TE uses a 64x16 centered ROI to respect internal
 * DMA storage and measure whole-ROI scheduling without pretending full-screen
 * 40MHz transfer fits one 60Hz refresh window. */
static uint64_t run(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io,
                    bool te, bool roi, unsigned frames, uint32_t window, uint32_t budget)
{
    unsigned w = roi ? 64 : 480, h = roi ? 16 : 480, tile = 16;
    check(esp_lcd_panel_set_gap(panel, roi ? 208 : 0, roi ? 232 : 0), "gap");
    pv2_lcd_spi_t *adapter;
    pv2_lcd_spi_config_t spi = {.panel = panel, .io = io, .format = PV2_FORMAT_RGB565, .byte_order = PV2_ORDER_SWAPPED, .max_inflight = 1, .psram_dma_direct = true};
    check(pv2_lcd_spi_create(&spi, &adapter), "adapter create");
    meter_t meter = {.window = te ? window : 0};
    check(pv2_lcd_spi_get_interface(adapter, &meter.real), "adapter interface");
    pv2_presenter_config_t cfg = {.backend_kind = te ? PV2_PRESENTER_TE : PV2_PRESENTER_GRAM, .timeout_ms = 2000,
                                  .backend = {.image_in_psram = te && !roi, .compose_count = te ? 2 : 0, .width = w, .height = h, .tile_rows = tile, .buffer_count = 2, .max_frames = 3,
                                              .format = PV2_FORMAT_RGB565, .panel_order = PV2_ORDER_SWAPPED,
                                              .panel = {&meter, metered_attach, metered_submit, metered_stop},
                                              .te_window_us = window, .te_transfer_budget_us = budget
                                             }
                                 };
    pv2_presenter_t *p;
    check(pv2_presenter_create(&cfg, &p), "presenter create");
    if (te) {
        portENTER_CRITICAL(&mux);
        te_presenter = p;
        portEXIT_CRITICAL(&mux);
    }
    ESP_LOGI(TAG, "BEGIN %s %ux%u frames=%u window=%"PRIu32" budget=%"PRIu32,
             te ? (roi ? "TE_ROI" : "TE_FULL_EDGE_SYNC_UNVERIFIED") : roi ? "GRAM_ROI" : "GRAM_FULL", w, h, frames, window, budget);
    pv2_presenter_receipt_t batch[2];
    unsigned batch_count = 0;
    for (unsigned f = 0; f < frames; f++) {
        pv2_frame_request_t req = {.full_redraw = true};
        check(pv2_presenter_begin_next_frame(p, &req), "begin");
        for (unsigned y = 0; y < h; y += tile) {
            pv2_lease_t l;
            check(pv2_presenter_acquire_buffer(p, &l), "acquire");
            uint16_t *pixels = l.surface.pixels;
            unsigned rows = h - y < tile ? h - y : tile;
            for (unsigned j = 0; j < rows; j++) {
                for (unsigned x = 0; x < w; x++) {
                    uint16_t c = colors[x * 8 / w];
                    if (roi && (x >= (f * 3) % (w - 12) && x < (f * 3) % (w - 12) + 12)) {
                        c = 0xffff;
                    }
                    if (x < 2 || x >= w - 2 || y + j < 2 || y + j >= h - 2) {
                        c = te ? 0x07e0 : 0xffff;
                    }
                    pixels[j * w + x] = c;
                }
            }
            pv2_area_t area = {0, y, w - 1, y + rows - 1};
            check(pv2_presenter_submit_buffer(p, &l, &area, w * 2), "submit");
        }
        check(pv2_presenter_commit_with_receipt(p, NULL, &batch[batch_count++]), "commit");
        if (te && batch_count < 2 && f + 1 < frames) {
            continue;
        }
        for (unsigned i = 0; i < batch_count; i++) {
            pv2_frame_status_t state;
            check(pv2_presenter_wait_frame(p, batch[i], PV2_COMPLETION_SOURCE_RELEASED, 2000, &state), "wait");
            check(state.result, "result");
            check(state.terminal && state.source_released ? ESP_OK : ESP_FAIL, "terminal");
            check(pv2_presenter_release_receipt(p, batch[i]), "release");
        }
        batch_count = 0;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    portENTER_CRITICAL(&mux);
    te_presenter = NULL;
    portEXIT_CRITICAL(&mux);
    check(pv2_presenter_stop(p, 2000), "stop");
    check(pv2_presenter_destroy(p), "destroy");
    check(pv2_lcd_spi_destroy(adapter), "adapter destroy");
    check(meter.done == meter.offered ? ESP_OK : ESP_FAIL, "callbacks balanced");
    ESP_LOGI(TAG, "window_overruns=%u", meter.overruns);
    check(meter.overruns == 0 ? ESP_OK : ESP_FAIL, "TE pulse overrun");
    ESP_LOGI(TAG, "PASS %s frames=%u tx=%u callbacks=%u max_tx_us=%"PRIu64" max_edge_age_us=%"PRIu64" internal=%u psram=%u",
             te ? (roi ? "TE_ROI" : "TE_FULL_EDGE_SYNC_UNVERIFIED") : roi ? "GRAM_ROI" : "GRAM_FULL", frames, meter.offered, meter.done, meter.max_tx, meter.max_edge_age,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return meter.max_tx;
}

static void run_rotation_async(esp_lcd_panel_handle_t panel, esp_lcd_panel_io_handle_t io,
                               bool te, size_t compose_count, uint32_t window, uint32_t budget)
{
    unsigned w = 480, h = 480, tile = 16;
    check(esp_lcd_panel_set_gap(panel, 0, 0), "gap");
    pv2_lcd_spi_t *adapter;
    pv2_lcd_spi_config_t spi = {.panel = panel, .io = io, .format = PV2_FORMAT_RGB565, .byte_order = PV2_ORDER_SWAPPED, .max_inflight = 1, .psram_dma_direct = true};
    check(pv2_lcd_spi_create(&spi, &adapter), "adapter create");
    meter_t meter = {.window = te ? window : 0};
    check(pv2_lcd_spi_get_interface(adapter, &meter.real), "adapter interface");
    pv2_presenter_config_t cfg = {.backend_kind = te ? PV2_PRESENTER_TE : PV2_PRESENTER_GRAM,
                                  .timeout_ms = 2000, .background_service = true,
                                  .backend = {.image_in_psram = te, .compose_count = te ? compose_count : 0,
                                              .width = w, .height = h, .tile_rows = tile, .buffer_count = 2, .max_frames = 3,
                                              .format = PV2_FORMAT_RGB565, .panel_order = PV2_ORDER_SWAPPED, .async_copy = true,
                                              .panel = {&meter, metered_attach, metered_submit, metered_stop},
                                              .te_window_us = window, .te_transfer_budget_us = budget
                                             }
                                 };
    pv2_presenter_t *p;
    check(pv2_presenter_create(&cfg, &p), "presenter create");
    if (te) {
        portENTER_CRITICAL(&mux);
        te_presenter = p;
        portEXIT_CRITICAL(&mux);
    }
    const pv2_rotation_t rots[] = {PV2_ROTATE_0, PV2_ROTATE_90, PV2_ROTATE_180, PV2_ROTATE_270, PV2_ROTATE_0};
    for (size_t r = 0; r < sizeof(rots) / sizeof(rots[0]); ++r) {
        check(pv2_presenter_set_rotation(p, rots[r], 2000), "set_rotation");
        for (unsigned f = 0; f < 2; ++f) {
            pv2_frame_request_t req = {.full_redraw = true};
            check(pv2_presenter_begin_next_frame(p, &req), "begin");
            for (unsigned y = 0; y < h; y += tile) {
                pv2_lease_t l;
                check(pv2_presenter_acquire_buffer(p, &l), "acquire");
                uint16_t *pixels = l.surface.pixels;
                unsigned rows = h - y < tile ? h - y : tile;
                for (unsigned j = 0; j < rows; j++) {
                    for (unsigned x = 0; x < w; x++) {
                        uint16_t c = colors[x * 8 / w];
                        if (x < 2 || x >= w - 2 || y + j < 2 || y + j >= h - 2) {
                            c = te ? 0x07e0 : 0xffff;
                        }
                        pixels[j * w + x] = c;
                    }
                }
                pv2_area_t area = {0, y, w - 1, y + rows - 1};
                check(pv2_presenter_submit_buffer(p, &l, &area, w * 2), "submit");
            }
            pv2_presenter_receipt_t receipt;
            check(pv2_presenter_commit_with_receipt(p, NULL, &receipt), "commit");
            pv2_frame_status_t state;
            check(pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_SOURCE_RELEASED, 2000, &state), "wait");
            check(state.result, "result");
            check(pv2_presenter_release_receipt(p, receipt), "release");
        }
    }
    portENTER_CRITICAL(&mux);
    te_presenter = NULL;
    portEXIT_CRITICAL(&mux);
    check(pv2_presenter_stop(p, 2000), "stop");
    check(pv2_presenter_destroy(p), "destroy");
    check(pv2_lcd_spi_destroy(adapter), "adapter destroy");
    check(meter.done == meter.offered ? ESP_OK : ESP_FAIL, "callbacks balanced");
    check(heap_caps_check_integrity_all(true) ? ESP_OK : ESP_FAIL, "heap integrity");
    ESP_LOGI(TAG, "COMBO_PASS kind=%s compose=%u rots=5 tx=%u max_tx_us=%"PRIu64" min_int=%u min_psram=%u free_int=%u free_psram=%u",
             te ? "TE_ASYNC_ROT" : "GRAM_ASYNC_ROT", (unsigned)compose_count, meter.offered, meter.max_tx,
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
void app_main(void)
{
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
    check(qspi_board_create(&panel, &io), "board init");
    gpio_config_t pin = {.pin_bit_mask = 1ULL << QSPI_TE_GPIO, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_ANYEDGE};
    check(gpio_config(&pin), "TE gpio");
    check(gpio_install_isr_service(0), "ISR service");
    check(gpio_isr_handler_add(QSPI_TE_GPIO, te_isr, NULL), "ISR handler");
    vTaskDelay(pdMS_TO_TICKS(1000));
    portENTER_CRITICAL(&mux);
    uint64_t min = period_min, max = period_max, high = high_min;
    unsigned n = edges;
    portEXIT_CRITICAL(&mux);
    ESP_LOGI(TAG, "TE measured edges=%u period_min=%"PRIu64" period_max=%"PRIu64" high_min=%"PRIu64, n, min, max, high);
    check(n > 10 && min > 1000 && min < 100000 ? ESP_OK : ESP_FAIL, "TE edges");
    run(panel, io, false, false, 8, 0, 0);
    /* Exercise real full-image ownership and DMA. This broad, explicit gate
     * only aligns with an edge; it is NOT a certified scan-phase window. */
    ESP_LOGW(TAG, "Full-frame edge synchronization only; optical phase model UNVERIFIED");
    run(panel, io, true, false, 12, (uint32_t)(3 * min), (uint32_t)(2 * min));
    run_rotation_async(panel, io, false, 0, 0, 0);
    run_rotation_async(panel, io, true, 1, (uint32_t)(3 * min), (uint32_t)(2 * min));
    run_rotation_async(panel, io, true, 2, (uint32_t)(3 * min), (uint32_t)(2 * min));
    /* ROI GRAM consists of one band. Budget accounts for its measured
     * maximum plus dispatch margin; must fit measured pulse high duration.
     * Pulse timing is only a diagnostic proxy, not a proven optical safe window. */
    uint64_t band_max = run(panel, io, false, true, 32, 0, 0);
    uint32_t budget = (uint32_t)(band_max + 100);
    uint32_t window = (uint32_t)(high < min ? high : min);
    ESP_LOGI(TAG, "TE admission pulse_window=%"PRIu32" budget=%"PRIu32"; optical safety UNVERIFIED", window, budget);
    check(budget < window ? ESP_OK : ESP_ERR_NOT_SUPPORTED, "ROI does not fit measured TE pulse");
    for (unsigned cycle = 0;; cycle++) {
        run(panel, io, true, true, 120, window, budget);
        ESP_LOGI(TAG, "CYCLE_PASS %u", cycle);
    }
}
