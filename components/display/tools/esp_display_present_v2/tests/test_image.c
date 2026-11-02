/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/present_image.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static bool fail_coverage_alloc;
void *__real_pv2_alloc(size_t);
void *__wrap_pv2_alloc(size_t bytes)
{
    return fail_coverage_alloc ? NULL : __real_pv2_alloc(bytes);
}
static void lazy_coverage(void)
{
    pv2_image_builder_t image;
    assert(pv2_image_init(&image, 1024, 600) == ESP_OK);
    assert(!image.coverage && image.coverage_bytes == 76800);
    assert(pv2_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(pv2_image_cover(&image, (pv2_rect_t) {
        0, 0, 1024, 300
    }) == ESP_OK);
    assert(pv2_image_cover(&image, (pv2_rect_t) {
        0, 300, 1024, 300
    }) == ESP_OK);
    assert(pv2_image_full(&image) && !image.coverage);
    assert(pv2_image_commit(&image) == ESP_OK);
    assert(pv2_image_begin(&image, 1, 0) == ESP_OK);
    for (unsigned i = 0; i < PV2_MAX_DAMAGE_AREAS; ++i)
        assert(pv2_image_cover(&image, (pv2_rect_t) {
        i * 2, 0, 1, 1
    }) == ESP_OK);
    fail_coverage_alloc = true;
    assert(pv2_image_cover(&image, (pv2_rect_t) {
        100, 0, 1, 1
    }) == ESP_ERR_NO_MEM);
    fail_coverage_alloc = false;
    assert(!image.coverage && !pv2_image_full(&image));
    pv2_image_prepare_repair(&image, false);
    pv2_rect_t area;
    assert(!pv2_image_next_repair(&image, &area));
    assert(pv2_image_commit(&image) == ESP_ERR_NO_MEM);
    pv2_image_cancel(&image);
    assert(pv2_image_begin(&image, 1, 0) == ESP_OK);
    for (unsigned i = 0; i < 40; ++i)
        assert(pv2_image_cover(&image, (pv2_rect_t) {
        i * 2, 0, 1, 1
    }) == ESP_OK);
    assert(image.coverage && !pv2_image_full(&image));
    pv2_image_cancel(&image);
    assert(pv2_image_begin(&image, 1, 0) == ESP_OK);
    assert(!image.coverage && image.coverage_error == ESP_OK);
    pv2_image_cancel(&image);
    pv2_image_deinit(&image);
}
static uint32_t rng = 0x42abcdef;
static unsigned random_u(unsigned limit)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % limit;
}
static void rectangle_reference(void)
{
    enum {W = 17, H = 9, N = W * H};
    pv2_image_builder_t image;
    assert(pv2_image_init(&image, W, H) == ESP_OK);
    assert(pv2_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(pv2_image_cover(&image, (pv2_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(pv2_image_commit(&image) == ESP_OK);
    for (unsigned trial = 0; trial < 256; ++trial) {
        bool written[N] = {0};
        bool repaired[N] = {0};
        assert(pv2_image_begin(&image, 1, 0) == ESP_OK);
        unsigned n = 1 + random_u(80);
        for (unsigned i = 0; i < n; ++i) {
            unsigned x = random_u(W), y = random_u(H);
            unsigned w = 1 + random_u(W - x), h = 1 + random_u(H - y);
            if (trial & 1) {
                w = h = 1;
            }
            assert(pv2_image_cover(&image, (pv2_rect_t) {
                x, y, w, h
            }) == ESP_OK);
            for (unsigned yy = y; yy < y + h; ++yy)
                for (unsigned xx = x; xx < x + w; ++xx) {
                    written[yy * W + xx] = true;
                }
        }
        bool full = true;
        for (unsigned i = 0; i < N; ++i) {
            full &= written[i];
        }
        assert(pv2_image_full(&image) == full);
        pv2_image_prepare_repair(&image, true);
        pv2_rect_t area;
        while (pv2_image_next_repair(&image, &area)) {
            for (unsigned y = area.y; y < area.y + area.height; ++y)
                for (unsigned x = area.x; x < area.x + area.width; ++x) {
                    assert(!written[y * W + x] && !repaired[y * W + x]);
                    repaired[y * W + x] = true;
                }
            assert(pv2_image_repaired(&image, &area) == ESP_OK);
        }
        for (unsigned i = 0; i < N; ++i) {
            assert(written[i] != repaired[i]);
        }
        pv2_image_cancel(&image);
    }
    pv2_image_deinit(&image);
}
int main(void)
{
    lazy_coverage();
    rectangle_reference();
    enum { W = 17, H = 9, N = W * H };
    pv2_image_builder_t b;
    assert(pv2_image_init(&b, W, H) == ESP_OK);
    uint16_t buffers[3][N], reference[N];
    memset(buffers, 0xa5, sizeof(buffers));
    memset(reference, 0, sizeof(reference));
    size_t baseline = SIZE_MAX;
    for (unsigned frame = 0; frame < 80; frame++) {
        size_t candidate = frame % 3;
        pv2_image_begin(&b, candidate, baseline);
        if (!frame) {
            for (unsigned i = 0; i < N; i++) {
                buffers[candidate][i] = reference[i] = i + 1;
            }
            pv2_image_cover(&b, (pv2_rect_t) {
                0, 0, W, H
            });
            assert(pv2_image_full(&b));
        } else {
            unsigned x = frame % W, y = (frame / 3) % H;
            reference[y * W + x] = buffers[candidate][y * W + x] = 1000 + frame;
            pv2_image_cover(&b, (pv2_rect_t) {
                x, y, 1, 1
            });
            assert(!pv2_image_full(&b));
        }
        pv2_image_prepare_repair(&b, false);
        pv2_rect_t a;
        while (pv2_image_next_repair(&b, &a)) {
            assert(baseline != SIZE_MAX);
            for (unsigned y = a.y; y < a.y + a.height; y++) {
                memcpy(buffers[candidate] + y * W + a.x, buffers[baseline] + y * W + a.x, a.width * 2);
            }
            pv2_image_repaired(&b, &a);
        }
        assert(!memcmp(buffers[candidate], reference, sizeof(reference)));
        if (frame > 2) {
            assert(b.repaired_pixels <= 2);
        }
        assert(pv2_image_commit(&b) == ESP_OK);
        baseline = candidate;
    }
    /* Cancel invalidates a contaminated allocation; history overflow cannot
     * turn a conservative dirty union into invented exact coverage. */
    size_t candidate = (baseline + 1) % 3;
    pv2_image_begin(&b, candidate, baseline);
    pv2_image_cover(&b, (pv2_rect_t) {
        0, 0, 1, 1
    });
    pv2_image_cancel(&b);
    assert(!b.versions[candidate]);
    pv2_image_begin(&b, candidate, baseline);
    for (unsigned i = 0; i < 40; i++)
        pv2_image_cover(&b, (pv2_rect_t) {
        (i * 2) % W, (i * 2) / W, 1, 1
    });
    assert(b.writes.full && !pv2_image_full(&b));
    pv2_image_prepare_repair(&b, false);
    pv2_rect_t a;
    while (pv2_image_next_repair(&b, &a)) {
        pv2_image_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 40);
    /* Adjacent horizontal bands coalesce without overflowing PV2_MAX_DAMAGE_AREAS. */
    pv2_image_cancel(&b);
    pv2_image_begin(&b, candidate, baseline);
    for (unsigned y = 0; y < 8; y++) {
        pv2_image_cover(&b, (pv2_rect_t) {
            2, y, 5, 1
        });
    }
    assert(!b.writes.full && b.writes.count == 1 && b.writes.areas[0].height == 8);
    /* Missing history safely requires a full repair, minus successful writes. */
    pv2_image_cancel(&b);
    b.versions[candidate] = 1;
    pv2_image_begin(&b, candidate, baseline);
    pv2_image_cover(&b, (pv2_rect_t) {
        3, 2, 2, 1
    });
    pv2_image_prepare_repair(&b, false);
    while (pv2_image_next_repair(&b, &a)) {
        pv2_image_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 2);
    /* DIRECT protects against writes outside producer-declared coverage. */
    pv2_image_cancel(&b);
    b.versions[candidate] = b.versions[baseline];
    pv2_image_begin(&b, candidate, baseline);
    pv2_image_cover(&b, (pv2_rect_t) {
        3, 2, 2, 1
    });
    pv2_image_prepare_repair(&b, true);
    while (pv2_image_next_repair(&b, &a)) {
        pv2_image_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 2);
    pv2_image_deinit(&b);
    return 0;
}
