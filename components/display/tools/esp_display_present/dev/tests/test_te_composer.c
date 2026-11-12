/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../src/backend/present_te_composer.h"
#include <assert.h>
#include "pixel/present_pixel_ops.h"
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x) == ESP_OK)
#define W 7
#define H 5
static uint32_t rng = 42;
static uint32_t next(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static void exercise(esp_display_present_engine_pixel_format_t format, esp_display_present_byte_order_t order)
{
    size_t bpp = format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    present_te_composer_t *c;
    present_te_composer_config_t cfg = {.width = W, .height = H, .format = format, .panel_order = order};
    OK(present_te_composer_create(&cfg, &c));
    uint8_t baseline[W * H * 3] = {0}, expected[sizeof(baseline)], pixels[9 * 7 * 3];
    size_t stride = 9 * bpp;
    esp_display_present_source_t source = {pixels, sizeof(pixels), {8, 7, stride, format, ESP_DISPLAY_PRESENT_ORDER_NATIVE}}, output;
    present_te_frame_t stale = {0};
    bool valid = false;
    for (unsigned i = 0; i < 120; i++) {
        present_te_frame_t f, second;
        OK(present_te_composer_begin(c, &f));
        assert(present_te_composer_begin(c, &second) == ESP_ERR_INVALID_STATE);
        assert(present_te_composer_complete(c, stale, ESP_OK) == ESP_ERR_INVALID_STATE);
        memcpy(expected, baseline, sizeof(expected));
        if (!valid) {
            assert(present_te_composer_seal(c, f) == ESP_ERR_INVALID_STATE);
        }
        unsigned writes = valid ? next() % 10 : 4;
        for (unsigned j = 0; j < writes; j++) {
            uint32_t x = next() % W, y = next() % H;
            uint32_t w = 1 + next() % (W - x), h = 1 + next() % (H - y);
            if (!valid && !j) {
                x = y = 0;
                w = W;
                h = H;
            }
            esp_display_present_rect_t crop = {1, 1, w, h};
            for (size_t k = 0; k < sizeof(pixels); k++) {
                pixels[k] = (uint8_t)(next() >> 24);
            }
            OK(present_te_composer_write(c, f, &source, &crop, x, y));
            for (uint32_t row = 0; row < h; row++)
                for (uint32_t col = 0; col < w; col++)
                    for (size_t k = 0; k < bpp; k++)
                        expected[((y + row) * W + x + col) * bpp + k] =
                            pixels[(row + 1) * stride + (col + 1) * bpp + (order == ESP_DISPLAY_PRESENT_ORDER_SWAPPED ? 1 - k : k)];
            memset(pixels, 0xff, sizeof(pixels)); /* No retained source borrow. */
        }
        esp_display_present_rect_t bad = {0, 0, W, H};
        assert(present_te_composer_write(c, f, &source, &bad, 1, 0) == ESP_ERR_INVALID_ARG);
        if (valid && i % 7 == 0) {
            OK(present_te_composer_cancel(c, f));
            stale = f;
            continue;
        }
        OK(present_te_composer_seal(c, f));
        assert(present_te_composer_write(c, f, &source, &bad, 0, 0) == ESP_ERR_INVALID_STATE);
        if (valid && i % 7 == 1) {
            OK(present_te_composer_cancel(c, f));
            stale = f;
            continue;
        }
        OK(present_te_composer_pin(c, f, &output));
        assert(memcmp(output.pixels, expected, W * H * bpp) == 0);
        assert(output.layout.stride_bytes == W * bpp && (uintptr_t)output.pixels % 64 == 0);
        assert(present_te_composer_cancel(c, f) == ESP_ERR_INVALID_STATE);
        assert(present_te_composer_destroy(c) == ESP_ERR_INVALID_STATE);
        const void *pinned = output.pixels;
        OK(present_te_composer_reject(c, f));
        OK(present_te_composer_pin(c, f, &output));
        assert(output.pixels == pinned && memcmp(output.pixels, expected, W * H * bpp) == 0);
        bool success = i % 5 != 2;
        OK(present_te_composer_complete(c, f, success ? ESP_OK : ESP_FAIL));
        assert(present_te_composer_complete(c, f, ESP_OK) == ESP_ERR_INVALID_STATE);
        if (success) {
            memcpy(baseline, expected, sizeof(expected));
            valid = true;
        }
        stale = f;
    }
    OK(present_te_composer_destroy(c));
}

static void coverage(void)
{
    present_te_composer_t *c, *other;
    present_te_composer_config_t cfg = {.width = 3, .height = 2, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel_order = ESP_DISPLAY_PRESENT_ORDER_NATIVE};
    OK(present_te_composer_create(&cfg, &c));
    OK(present_te_composer_create(&cfg, &other));
    present_te_frame_t f, foreign;
    OK(present_te_composer_begin(c, &f));
    OK(present_te_composer_begin(other, &foreign));
    assert(present_te_composer_cancel(c, foreign) == ESP_ERR_INVALID_STATE);
    uint16_t row[] = {1, 2, 3};
    esp_display_present_source_t s = {row, sizeof(row), {3, 1, 6, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}}, out;
    esp_display_present_rect_t a = {0, 0, 3, 1};
    OK(present_te_composer_write(c, f, &s, &a, 0, 0));
    OK(present_te_composer_write(c, f, &s, &a, 0, 0));
    assert(present_te_composer_seal(c, f) == ESP_ERR_INVALID_STATE); /* overlap != coverage */
    OK(present_te_composer_write(c, f, &s, &a, 0, 1));
    OK(present_te_composer_seal(c, f));
    OK(present_te_composer_pin(c, f, &out));
    OK(present_te_composer_complete(c, f, ESP_FAIL));
    OK(present_te_composer_begin(c, &f));
    assert(present_te_composer_seal(c, f) == ESP_ERR_INVALID_STATE);
    OK(present_te_composer_cancel(c, f));
    OK(present_te_composer_cancel(other, foreign));
    OK(present_te_composer_destroy(other));
    OK(present_te_composer_destroy(c));
    cfg.width = cfg.height = UINT32_MAX;
    assert(present_te_composer_create(&cfg, &c) == ESP_ERR_INVALID_ARG && c == NULL);
}

typedef struct {
    unsigned remaining;
    esp_err_t error;
} repair_failure_t;
static esp_err_t repair_copy(void *user, const esp_display_present_copy_request_t *request)
{
    repair_failure_t *failure = user;
    if (failure->remaining && --failure->remaining == 0) {
        return failure->error;
    }
    return esp_display_present_pixel_execute_cpu(request);
}

static void synchronous_repair_retry(void)
{
    for (unsigned fail_at = 1; fail_at <= 2; ++fail_at) {
        for (unsigned timeout = 0; timeout < 2; ++timeout) {
            repair_failure_t failure = {.error = timeout ? ESP_ERR_TIMEOUT : ESP_FAIL};
            present_te_composer_config_t cfg = {.width = 4, .height = 4, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                                .pixel = {.max_jobs = 1, .execute = repair_copy, .execute_user = &failure}
                                               };
            present_te_composer_t *composer;
            OK(present_te_composer_create(&cfg, &composer));
            uint16_t pixels[16];
            for (unsigned i = 0; i < 16; ++i) {
                pixels[i] = 0x1234;
            }
            esp_display_present_source_t source = {pixels, sizeof(pixels), {4, 4, 8, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}}, output;
            esp_display_present_rect_t area = {0, 0, 4, 4};
            present_te_frame_t frame;
            OK(present_te_composer_begin(composer, &frame));
            OK(present_te_composer_write(composer, frame, &source, &area, 0, 0));
            OK(present_te_composer_seal(composer, frame));
            OK(present_te_composer_pin(composer, frame, &output));
            OK(present_te_composer_complete(composer, frame, ESP_OK));
            OK(present_te_composer_begin(composer, &frame));
            for (unsigned i = 0; i < 16; ++i) {
                pixels[i] = 0xabcd;
            }
            area = (esp_display_present_rect_t) {
                0, 0, 2, 2
            };
            OK(present_te_composer_write(composer, frame, &source, &area, 1, 1));
            failure.remaining = fail_at;
            assert(present_te_composer_seal(composer, frame) == failure.error);
            assert(present_te_composer_pin(composer, frame, &output) == ESP_ERR_INVALID_STATE);
            OK(present_te_composer_seal(composer, frame));
            OK(present_te_composer_pin(composer, frame, &output));
            for (unsigned y = 0; y < 4; ++y) {
                for (unsigned x = 0; x < 4; ++x) {
                    uint16_t value;
                    memcpy(&value, (uint8_t *)output.pixels + (y * 4 + x) * 2, 2);
                    assert(value == (x >= 1 && x < 3 && y >= 1 && y < 3 ? 0xabcd : 0x1234));
                }
            }
            OK(present_te_composer_complete(composer, frame, ESP_OK));
            OK(present_te_composer_destroy(composer));
        }
    }
}

int main(void)
{
    synchronous_repair_retry();
    coverage();
    exercise(ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE);
    exercise(ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_SWAPPED);
    exercise(ESP_DISPLAY_PRESENT_FORMAT_RGB888, ESP_DISPLAY_PRESENT_ORDER_NATIVE);
    puts("TE CPU composition and ownership passed");
    return 0;
}
