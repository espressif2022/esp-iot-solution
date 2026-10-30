/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/backend/te_composer.h"
#include <assert.h>
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

static void exercise(pv2_pixel_format_t format, pv2_byte_order_t order)
{
    size_t bpp = format == PV2_FORMAT_RGB565 ? 2 : 3;
    pv2_te_composer_t *c;
    pv2_te_composer_config_t cfg = {.width = W, .height = H, .format = format, .panel_order = order};
    OK(pv2_te_composer_create(&cfg, &c));
    uint8_t baseline[W * H * 3] = {0}, expected[sizeof(baseline)], pixels[9 * 7 * 3];
    size_t stride = 9 * bpp;
    pv2_source_t source = {pixels, sizeof(pixels), {8, 7, stride, format, PV2_ORDER_NATIVE}}, output;
    pv2_te_frame_t stale = {0};
    bool valid = false;
    for (unsigned i = 0; i < 120; i++) {
        pv2_te_frame_t f, second;
        OK(pv2_te_composer_begin(c, &f));
        assert(pv2_te_composer_begin(c, &second) == ESP_ERR_INVALID_STATE);
        assert(pv2_te_composer_complete(c, stale, ESP_OK) == ESP_ERR_INVALID_STATE);
        memcpy(expected, baseline, sizeof(expected));
        uint64_t ready = 1000u * (i + 1);
        if (!valid) {
            assert(pv2_te_composer_seal(c, f, ready) == ESP_ERR_INVALID_STATE);
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
            pv2_rect_t crop = {1, 1, w, h};
            for (size_t k = 0; k < sizeof(pixels); k++) {
                pixels[k] = (uint8_t)(next() >> 24);
            }
            OK(pv2_te_composer_write(c, f, &source, &crop, x, y));
            for (uint32_t row = 0; row < h; row++)
                for (uint32_t col = 0; col < w; col++)
                    for (size_t k = 0; k < bpp; k++)
                        expected[((y + row) * W + x + col) * bpp + k] =
                            pixels[(row + 1) * stride + (col + 1) * bpp + (order == PV2_ORDER_SWAPPED ? 1 - k : k)];
            memset(pixels, 0xff, sizeof(pixels)); /* No retained source borrow. */
        }
        pv2_rect_t bad = {0, 0, W, H};
        assert(pv2_te_composer_write(c, f, &source, &bad, 1, 0) == ESP_ERR_INVALID_ARG);
        if (valid && i % 7 == 0) {
            OK(pv2_te_composer_cancel(c, f));
            stale = f;
            continue;
        }
        OK(pv2_te_composer_seal(c, f, ready));
        assert(pv2_te_composer_write(c, f, &source, &bad, 0, 0) == ESP_ERR_INVALID_STATE);
        if (valid && i % 7 == 1) {
            OK(pv2_te_composer_cancel(c, f));
            stale = f;
            continue;
        }
        assert(pv2_te_composer_offer(c, f, ready - 1, ready, 10, &output) == ESP_ERR_TIMEOUT);
        assert(output.pixels == NULL);
        assert(pv2_te_composer_offer(c, f, ready, ready + 10, 10, &output) == ESP_ERR_TIMEOUT);
        assert(pv2_te_composer_offer(c, f, ready + 1, ready, 10, &output) == ESP_ERR_INVALID_ARG);
        OK(pv2_te_composer_offer(c, f, ready + 20, ready + 21, 10, &output));
        assert(memcmp(output.pixels, expected, W * H * bpp) == 0);
        assert(output.layout.stride_bytes == W * bpp && (uintptr_t)output.pixels % 64 == 0);
        assert(pv2_te_composer_cancel(c, f) == ESP_ERR_INVALID_STATE);
        assert(pv2_te_composer_destroy(c) == ESP_ERR_INVALID_STATE);
        const void *pinned = output.pixels;
        OK(pv2_te_composer_reject(c, f));
        assert(pv2_te_composer_offer(c, f, ready + 20, ready + 22, 10, &output) == ESP_ERR_TIMEOUT);
        OK(pv2_te_composer_offer(c, f, ready + 40, ready + 41, 10, &output));
        assert(output.pixels == pinned && memcmp(output.pixels, expected, W * H * bpp) == 0);
        bool success = i % 5 != 2;
        OK(pv2_te_composer_complete(c, f, success ? ESP_OK : ESP_FAIL));
        assert(pv2_te_composer_complete(c, f, ESP_OK) == ESP_ERR_INVALID_STATE);
        if (success) {
            memcpy(baseline, expected, sizeof(expected));
            valid = true;
        }
        stale = f;
    }
    OK(pv2_te_composer_destroy(c));
}

static void coverage(void)
{
    pv2_te_composer_t *c, *other;
    pv2_te_composer_config_t cfg = {.width = 3, .height = 2, .format = PV2_FORMAT_RGB565, .panel_order = PV2_ORDER_NATIVE};
    OK(pv2_te_composer_create(&cfg, &c));
    OK(pv2_te_composer_create(&cfg, &other));
    pv2_te_frame_t f, foreign;
    OK(pv2_te_composer_begin(c, &f));
    OK(pv2_te_composer_begin(other, &foreign));
    assert(pv2_te_composer_cancel(c, foreign) == ESP_ERR_INVALID_STATE);
    uint16_t row[] = {1, 2, 3};
    pv2_source_t s = {row, sizeof(row), {3, 1, 6, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}}, out;
    pv2_rect_t a = {0, 0, 3, 1};
    OK(pv2_te_composer_write(c, f, &s, &a, 0, 0));
    OK(pv2_te_composer_write(c, f, &s, &a, 0, 0));
    assert(pv2_te_composer_seal(c, f, 0) == ESP_ERR_INVALID_STATE); /* overlap != coverage */
    OK(pv2_te_composer_write(c, f, &s, &a, 0, 1));
    OK(pv2_te_composer_seal(c, f, 0));
    OK(pv2_te_composer_offer(c, f, 0, 0, 1, &out));
    OK(pv2_te_composer_complete(c, f, ESP_FAIL));
    OK(pv2_te_composer_begin(c, &f));
    assert(pv2_te_composer_seal(c, f, 1) == ESP_ERR_INVALID_STATE);
    OK(pv2_te_composer_cancel(c, f));
    OK(pv2_te_composer_cancel(other, foreign));
    OK(pv2_te_composer_destroy(other));
    OK(pv2_te_composer_destroy(c));
    cfg.width = cfg.height = UINT32_MAX;
    assert(pv2_te_composer_create(&cfg, &c) == ESP_ERR_INVALID_ARG && c == NULL);
}
int main(void)
{
    coverage();
    exercise(PV2_FORMAT_RGB565, PV2_ORDER_NATIVE);
    exercise(PV2_FORMAT_RGB565, PV2_ORDER_SWAPPED);
    exercise(PV2_FORMAT_RGB888, PV2_ORDER_NATIVE);
    puts("TE CPU composition and admission passed");
    return 0;
}
