/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/present_image.h"
#include <assert.h>
#include <string.h>

int main(void)
{
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
        i % W, i / W, 1, 1
    });
    assert(b.writes.full && !pv2_image_full(&b));
    pv2_image_prepare_repair(&b, false);
    pv2_rect_t a;
    while (pv2_image_next_repair(&b, &a)) {
        pv2_image_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 40);
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
