/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include "board.h"
#include "present_presenter.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pv2_board";
static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000, 0xffe0, 0x07ff, 0xf81f};
static const struct {
    const char *name;
    pv2_present_mode_t mode;
    size_t buffers;
    pv2_render_contract_t contract;
} cases[] = {
    {"DOUBLE_FULL", PV2_MODE_DOUBLE_FULL, 2, PV2_CONTRACT_FULL},
    {"TRIPLE_FULL", PV2_MODE_TRIPLE_FULL, 3, PV2_CONTRACT_FULL},
    {"DOUBLE_DIRECT", PV2_MODE_DOUBLE_DIRECT, 2, PV2_CONTRACT_DIRECT},
    {"DOUBLE_PARTIAL", PV2_MODE_DOUBLE_PARTIAL, 2, PV2_CONTRACT_PARTITION},
    {"TRIPLE_PARTIAL", PV2_MODE_TRIPLE_PARTIAL, 3, PV2_CONTRACT_PARTITION},
};
#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))
#define FRAME_COUNT 48
#define TILE_ROWS 11

static void check(esp_err_t err, const char *operation)
{
    if (err == ESP_OK) {
        return;
    }
    while (true) {
        ESP_LOGE(TAG, "FAIL %s: %s; resources retained, reset to retry", operation, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* Tap only descriptors; all actual callbacks and submissions stay in V2. */
typedef struct {
    pv2_panel_adapter_t real;
    pv2_surface_t submitted;
    unsigned submissions;
} tap_t;
static esp_err_t tap_attach(void *context, pv2_panel_event_fn callback, void *user)
{
    tap_t *tap = context;
    return tap->real.attach(tap->real.context, callback, user);
}
static pv2_tx_result_t tap_submit(void *context, const pv2_surface_t *surface,
                                  const pv2_rect_t *area, uint64_t cookie)
{
    tap_t *tap = context;
    pv2_tx_result_t result = tap->real.submit(tap->real.context, surface, area, cookie);
    if (result.acceptance == PV2_TX_ACCEPTED) {
        tap->submitted = *surface;
        tap->submissions++;
    }
    return result;
}
static esp_err_t tap_quiesce(void *context, uint32_t timeout)
{
    tap_t *tap = context;
    return tap->real.quiesce(tap->real.context, timeout);
}

static pv2_rect_t moving(unsigned frame)
{
    return (pv2_rect_t) {
        7 + frame * 13 % (WIDTH - 60), 65 + frame * 7 % (HEIGHT - 150), 37, 59
    };
}
static uint16_t reference(unsigned x, unsigned y, unsigned frame, unsigned test)
{
    pv2_rect_t box = moving(frame);
    if (x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height) {
        return 0xffff;
    }
    if (x < 3 || y < 3 || x >= WIDTH - 3 || y >= HEIGHT - 3) {
        return 0xffff;
    }
    /* Top row identifies mode by 1..5 white squares, left to right. */
    if (y >= 12 && y < 36 && x >= 12 && x < 12 + CASE_COUNT * 32) {
        return (x - 12) / 32 <= test && (x - 12) % 32 < 24 ? 0xffff : 0;
    }
    return colors[x * 8 / WIDTH];
}

static void render_region(pv2_presenter_t *p, pv2_render_contract_t contract,
                          pv2_rect_t area, unsigned frame, unsigned test)
{
    for (unsigned offset = 0; offset < area.height;) {
        pv2_lease_t lease;
        check(pv2_presenter_acquire_buffer(p, &lease), "acquire");
        unsigned rows = area.height - offset;
        if (contract == PV2_CONTRACT_PARTITION && rows > TILE_ROWS) {
            rows = TILE_ROWS;
        }
        /* Padded, odd-width dirty tiles exercise submit-time stride packing. */
        size_t stride = contract == PV2_CONTRACT_PARTITION ? (area.width + (area.width < WIDTH ? 3 : 0)) * 2
                        : lease.surface.layout.stride_bytes;
        for (unsigned y = 0; y < rows; ++y) {
            unsigned dy = area.y + offset + y;
            uint16_t *row = (uint16_t *)((uint8_t *)lease.surface.pixels +
                                         (contract == PV2_CONTRACT_PARTITION ? y : dy) * stride);
            for (unsigned x = 0; x < area.width; ++x) {
                row[contract == PV2_CONTRACT_PARTITION ? x : area.x + x] = reference(area.x + x, dy, frame, test);
            }
        }
        pv2_area_t submit = {area.x, area.y + offset, area.x + area.width - 1, area.y + offset + rows - 1};
        check(pv2_presenter_submit_buffer(p, &lease, &submit, stride), "submit region");
        offset += rows;
    }
}

static void verify(const pv2_surface_t *surface, unsigned frame, unsigned test)
{
    for (unsigned y = 0; y < HEIGHT; ++y) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)surface->pixels + y * surface->layout.stride_bytes);
        for (unsigned x = 0; x < WIDTH; ++x) {
            uint16_t expected = reference(x, y, frame, test);
            if (row[x] != expected) {
                ESP_LOGE(TAG, "PIXEL mode=%s frame=%u x=%u y=%u actual=%04x expected=%04x",
                         cases[test].name, frame, x, y, row[x], expected);
                check(ESP_FAIL, "pixel compare");
            }
        }
    }
}

static void run_case(unsigned cycle, unsigned test)
{
    ESP_LOGI(TAG, "BEGIN cycle=%u mode=%s buffers=%u", cycle, cases[test].name, (unsigned)cases[test].buffers);
    esp_lcd_panel_handle_t panel;
    check(board_panel_create(&panel, cases[test].buffers), "board panel create");
    pv2_lcd_scanout_t *adapter;
    pv2_lcd_scanout_config_t scan = {.panel = panel, .kind = SCANOUT_KIND,
                                     .width = WIDTH, .height = HEIGHT, .format = PV2_FORMAT_RGB565, .buffer_count = cases[test].buffers,
                                     .rgb_bounce_buffer_mode = SCANOUT_KIND == PV2_LCD_RGB_BOUNCE, .delete_panel_on_stop = true
                                    };
    check(pv2_lcd_scanout_create(&scan, &adapter), "scanout create");
    void *buffers[3];
    size_t count;
    check(pv2_lcd_scanout_get_buffers(adapter, buffers, 3, &count), "get buffers");
    tap_t tap = {0};
    check(pv2_lcd_scanout_get_interface(adapter, &tap.real), "interface");
    pv2_presenter_config_t cfg = {.backend_kind = PV2_PRESENTER_FB, .mode = cases[test].mode,
                                  .timeout_ms = 2000, .backend = {.width = WIDTH, .height = HEIGHT, .tile_rows = TILE_ROWS,
                                                                  .format = PV2_FORMAT_RGB565, .buffer_count = count, .max_frames = 3,
                                                                  .external_buffers = buffers, .external_buffer_bytes = WIDTH * HEIGHT * 2,
                                                                  .initial_scanout_buffer = buffers[0], .panel = {&tap, tap_attach, tap_submit, tap_quiesce}
                                                                 }
                                 };
    pv2_presenter_t *presenter;
    check(pv2_presenter_create(&cfg, &presenter), "presenter create");
    uint64_t max_wait = 0;
    for (unsigned frame = 0; frame < FRAME_COUNT; ++frame) {
        bool full = frame == 0 || cases[test].contract == PV2_CONTRACT_FULL;
        pv2_rect_t dirty[2] = {moving(frame ? frame - 1 : 0), moving(frame)};
        pv2_frame_request_t request = {.full_redraw = full, .damage = full ? NULL : dirty, .damage_count = full ? 0 : 2};
        check(pv2_presenter_begin_next_frame(presenter, &request), "begin");
        if (full) {
            render_region(presenter, cases[test].contract, (pv2_rect_t) {
                0, 0, WIDTH, HEIGHT
            }, frame, test);
        } else {
            render_region(presenter, cases[test].contract, dirty[0], frame, test);
            render_region(presenter, cases[test].contract, dirty[1], frame, test);
        }
        pv2_presenter_receipt_t receipt;
        int64_t start = esp_timer_get_time();
        check(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt), "commit");
        pv2_frame_status_t status;
        check(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 2000, &status), "wait");
        check(status.result, "frame result");
        check(status.present_confirmed && status.terminal ? ESP_OK : ESP_FAIL, "confirmed terminal");
        uint64_t elapsed = esp_timer_get_time() - start;
        if (elapsed > max_wait) {
            max_wait = elapsed;
        }
        check(tap.submissions == frame + 1 ? ESP_OK : ESP_FAIL, "submission count");
        verify(&tap.submitted, frame, test);
        check(pv2_presenter_release_receipt(presenter, receipt), "release");
        check(pv2_presenter_service(presenter), "service");
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    pv2_presenter_status_t state;
    check(pv2_presenter_get_status(presenter, &state), "status");
    check(state.first_error, "sticky fault");
    check(state.pending_frames == 0 ? ESP_OK : ESP_FAIL, "receipts drained");
    ESP_LOGI(TAG, "PASS cycle=%u mode=%s frames=%u pixels=ALL max_wait_us=%" PRIu64 " pending=0 sticky=ESP_OK",
             cycle, cases[test].name, FRAME_COUNT, max_wait);
    check(pv2_presenter_stop(presenter, 2000), "stop");
    check(pv2_presenter_destroy(presenter), "presenter destroy");
    check(pv2_lcd_scanout_destroy(adapter), "adapter destroy");
    check(board_resources_destroy(), "board resources destroy");
    check(heap_caps_check_integrity_all(true) ? ESP_OK : ESP_FAIL, "heap integrity");
    ESP_LOGI(TAG, "CLEAN cycle=%u mode=%s internal=%u psram=%u", cycle, cases[test].name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
void app_main(void)
{
    ESP_LOGI(TAG, "V2 SUITE2 %s %dx%d RGB565 CPU", BOARD_NAME, WIDTH, HEIGHT);
    for (unsigned cycle = 0; ; ++cycle) {
        for (unsigned test = 0; test < CASE_COUNT; ++test) {
            run_case(cycle, test);
        }
        ESP_LOGI(TAG, "SUITE_PASS cycle=%u modes=%u confirmed_frames=%u", cycle, (unsigned)CASE_COUNT, (unsigned)CASE_COUNT * FRAME_COUNT);
    }
}
