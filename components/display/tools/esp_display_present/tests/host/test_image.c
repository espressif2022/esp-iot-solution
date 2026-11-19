/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../../src/backend/present_image_repair.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static bool fail_coverage_alloc;
void *__real_present_alloc(size_t);
void *__wrap_present_alloc(size_t bytes)
{
    return fail_coverage_alloc ? NULL : __real_present_alloc(bytes);
}
static void lazy_coverage(void)
{
    present_image_builder_t image;
    assert(present_image_init(&image, 1024, 600, 3) == ESP_OK);
    assert(!image.coverage && image.coverage_bytes == 76800);
    assert(present_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, 1024, 300
    }) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 300, 1024, 300
    }) == ESP_OK);
    assert(present_image_full(&image) && !image.coverage);
    assert(present_image_commit(&image) == ESP_OK);
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    for (unsigned i = 0; i < PRESENT_MAX_DAMAGE_AREAS; ++i)
        assert(present_image_cover(&image, (esp_display_present_rect_t) {
        i * 2, 0, 1, 1
    }) == ESP_OK);
    fail_coverage_alloc = true;
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        100, 0, 1, 1
    }) == ESP_ERR_NO_MEM);
    fail_coverage_alloc = false;
    assert(!image.coverage && !present_image_full(&image));
    present_image_prepare_repair(&image, false);
    esp_display_present_rect_t area;
    assert(!present_image_next_repair(&image, &area));
    assert(present_image_commit(&image) == ESP_ERR_NO_MEM);
    present_image_cancel(&image);
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    for (unsigned i = 0; i < 40; ++i)
        assert(present_image_cover(&image, (esp_display_present_rect_t) {
        i * 2, 0, 1, 1
    }) == ESP_OK);
    assert(image.coverage && !present_image_full(&image));
    present_image_cancel(&image);
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    assert(!image.coverage && image.coverage_error == ESP_OK);
    present_image_cancel(&image);
    present_image_deinit(&image);
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
    present_image_builder_t image;
    assert(present_image_init(&image, W, H, 3) == ESP_OK);
    assert(present_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);
    for (unsigned trial = 0; trial < 256; ++trial) {
        bool written[N] = {0};
        bool repaired[N] = {0};
        assert(present_image_begin(&image, 1, 0) == ESP_OK);
        unsigned n = 1 + random_u(80);
        for (unsigned i = 0; i < n; ++i) {
            unsigned x = random_u(W), y = random_u(H);
            unsigned w = 1 + random_u(W - x), h = 1 + random_u(H - y);
            if (trial & 1) {
                w = h = 1;
            }
            assert(present_image_cover(&image, (esp_display_present_rect_t) {
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
        assert(present_image_full(&image) == full);
        present_image_prepare_repair(&image, true);
        esp_display_present_rect_t area;
        while (present_image_next_repair(&image, &area)) {
            for (unsigned y = area.y; y < area.y + area.height; ++y)
                for (unsigned x = area.x; x < area.x + area.width; ++x) {
                    assert(!written[y * W + x] && !repaired[y * W + x]);
                    repaired[y * W + x] = true;
                }
            assert(present_image_mark_repaired(&image, &area) == ESP_OK);
        }
        for (unsigned i = 0; i < N; ++i) {
            assert(written[i] != repaired[i]);
        }
        present_image_cancel(&image);
    }
    present_image_deinit(&image);
}
/* Compare tall coverage bands against independent per-pixel membership. Include
 * a hole, unions of writes/seeds, offset resumes, and rejected repair retries. */
static void band_reference(void)
{
    enum { W = 19, H = 137, N = W * H };
    present_image_builder_t image;
    assert(present_image_init(&image, W, H, 3) == ESP_OK);
    assert(present_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);
    for (unsigned trial = 0; trial < 128; ++trial) {
        bool initialized[N] = {0}, repaired[N] = {0};
        assert(present_image_begin(&image, 1, 0) == ESP_OK);
        unsigned count = trial == 0 ? 2 : 1 + random_u(24);
        for (unsigned i = 0; i < count; ++i) {
            unsigned x = random_u(W), y = random_u(H);
            esp_display_present_rect_t a = {x, y, 1 + random_u(W - x), 1 + random_u(H - y)};
            if (trial == 0) {
                a = (esp_display_present_rect_t) {
                    0, i ? 70 : 0, W, i ? H - 70 : 69
                };
            }
            assert((i & 1 ? present_image_seed(&image, a) : present_image_cover(&image, a)) == ESP_OK);
            for (unsigned yy = a.y; yy < a.y + a.height; ++yy)
                for (unsigned xx = a.x; xx < a.x + a.width; ++xx) {
                    initialized[yy * W + xx] = true;
                }
        }
        present_image_prepare_repair(&image, true);
        esp_display_present_rect_t area, retry;
        unsigned steps = 0;
        while (present_image_next_repair(&image, &area)) {
            assert(++steps <= N);
            assert(present_image_next_repair(&image, &retry));
            assert(memcmp(&area, &retry, sizeof(area)) == 0);
            assert(area.width && area.height && area.x + area.width <= W && area.y + area.height <= H);
            for (unsigned yy = area.y; yy < area.y + area.height; ++yy)
                for (unsigned xx = area.x; xx < area.x + area.width; ++xx) {
                    assert(!initialized[yy * W + xx] && !repaired[yy * W + xx]);
                    repaired[yy * W + xx] = true;
                }
            assert(present_image_mark_repaired(&image, &area) == ESP_OK);
        }
        for (unsigned i = 0; i < N; ++i) {
            assert(initialized[i] != repaired[i]);
        }
        present_image_cancel(&image);
    }
    present_image_deinit(&image);
}
static void mid_repair_bitmap_fallback(void)
{
    enum { W = 64, H = 64, N = W * H };
    present_image_builder_t image;
    assert(present_image_init(&image, W, H, 3) == ESP_OK);
    assert(present_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);

    bool initialized[N] = {0}, repaired[N] = {0};
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    /* Write 30 disjoint 1x1 pixels (< PRESENT_MAX_DAMAGE_AREAS) so exact stays
     * in rectangle mode before repair starts, then crosses 32 mid-repair. */
    for (unsigned i = 0; i < 30; ++i) {
        uint32_t x = (i * 2) % W;
        uint32_t y = (i / 10) * 4;
        esp_display_present_rect_t a = {x, y, 1, 1};
        assert((i & 1 ? present_image_seed(&image, a) : present_image_cover(&image, a)) == ESP_OK);
        initialized[y * W + x] = true;
    }
    assert(!image.coverage);

    present_image_prepare_repair(&image, true);
    esp_display_present_rect_t area;
    bool saw_bitmap_switch = false;
    unsigned steps = 0;
    while (present_image_next_repair(&image, &area)) {
        assert(++steps <= N);
        assert(area.width && area.height && area.x + area.width <= W && area.y + area.height <= H);
        for (unsigned yy = area.y; yy < area.y + area.height; ++yy) {
            for (unsigned xx = area.x; xx < area.x + area.width; ++xx) {
                assert(!initialized[yy * W + xx] && !repaired[yy * W + xx]);
                repaired[yy * W + xx] = true;
            }
        }
        assert(present_image_mark_repaired(&image, &area) == ESP_OK);
        if (image.coverage) {
            saw_bitmap_switch = true;
        }
    }
    assert(saw_bitmap_switch);
    for (unsigned i = 0; i < N; ++i) {
        assert(initialized[i] != repaired[i]);
    }
    present_image_cancel(&image);
    present_image_deinit(&image);
}
static unsigned rect_pixels(esp_display_present_rect_t area)
{
    return area.width * area.height;
}
/* Seeded baseline pixels are not application coverage and are not repaired again.
 * A seed that only overlaps part of the historical damage still leaves the rest. */
static void seeded_baseline(void)
{
    enum { W = 8, H = 4 };
    present_image_builder_t image;
    assert(present_image_init(&image, W, H, 3) == ESP_OK);
    assert(present_image_begin(&image, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);

    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    assert(present_image_seed(&image, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        1, 1, 1, 1
    }) == ESP_OK);
    assert(!present_image_full(&image));
    assert(image.writes.count == 1 && !image.writes.full);
    present_image_prepare_repair(&image, true);
    esp_display_present_rect_t area;
    assert(!present_image_next_repair(&image, &area));
    assert(image.repaired_pixels == 0);
    assert(present_image_commit(&image) == ESP_OK);
    assert(image.stale[0].count == 1 && !image.stale[0].full);

    /* Replace that one-pixel publication with a wide historical write, then
     * seed only the opposite corner. Repair keeps the historical pixels. */
    assert(present_image_begin(&image, 0, 1) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        2, 0, 6, 4
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    assert(present_image_seed(&image, (esp_display_present_rect_t) {
        0, 0, 2, 2
    }) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        0, 0, 1, 1
    }) == ESP_OK);
    present_image_prepare_repair(&image, false);
    unsigned repaired = 0;
    while (present_image_next_repair(&image, &area)) {
        assert(area.x >= 2);
        repaired += rect_pixels(area);
        assert(present_image_mark_repaired(&image, &area) == ESP_OK);
    }
    assert(repaired == 24);
    assert(!present_image_full(&image));
    assert(present_image_commit(&image) == ESP_OK);

    /* The seed bbox overlaps the historical damage. The overlap is not copied
     * again; the historical pixels outside the bbox still are. */
    assert(present_image_begin(&image, 0, 1) == ESP_OK);
    assert(present_image_cover(&image, (esp_display_present_rect_t) {
        6, 0, 2, 2
    }) == ESP_OK);
    assert(present_image_commit(&image) == ESP_OK);
    assert(present_image_begin(&image, 1, 0) == ESP_OK);
    assert(present_image_seed(&image, (esp_display_present_rect_t) {
        5, 0, 2, 2
    }) == ESP_OK);
    present_image_prepare_repair(&image, false);
    repaired = 0;
    while (present_image_next_repair(&image, &area)) {
        assert(!(area.x < 7 && area.x + area.width > 5 && area.y < 2));
        repaired += rect_pixels(area);
        assert(present_image_mark_repaired(&image, &area) == ESP_OK);
    }
    assert(repaired == 2);
    present_image_deinit(&image);
}
/* Bitmap row coalescing must also stop at a separately pre-seeded row. */
static void seeded_bitmap_band(void)
{
    enum {W = 80, H = 8};
    present_image_builder_t b;
    assert(present_image_init(&b, W, H, 2) == ESP_OK);
    assert(present_image_begin(&b, 0, SIZE_MAX) == ESP_OK);
    assert(present_image_cover(&b, (esp_display_present_rect_t) {
        0, 0, W, H
    }) == ESP_OK);
    assert(present_image_commit(&b) == ESP_OK);
    assert(present_image_begin(&b, 1, 0) == ESP_OK);
    for (unsigned x = 0; x <= PRESENT_MAX_DAMAGE_AREAS; ++x) {
        assert(present_image_cover(&b, (esp_display_present_rect_t) {
            x * 2, 0, 1, 1
        }) == ESP_OK);
    }
    assert(b.coverage);
    assert(present_image_seed(&b, (esp_display_present_rect_t) {
        0, 4, W, 1
    }) == ESP_OK);
    present_image_prepare_repair(&b, false);
    esp_display_present_rect_t area;
    while (present_image_next_repair(&b, &area)) {
        assert(area.y > 4 || area.y + area.height <= 4);
        assert(present_image_mark_repaired(&b, &area) == ESP_OK);
    }
    assert(b.repaired_pixels == W * H - W - (PRESENT_MAX_DAMAGE_AREAS + 1));
    present_image_cancel(&b);
    present_image_deinit(&b);
}

/* Reuse arbitrarily old images, overflow conservative history, and cancel
 * contaminated candidates. Compare every pixel with an independent model. */
static void stale_slot_reference(void)
{
    enum {W = 19, H = 13, N = W * H};
    uint32_t previous_rng = rng;
    for (size_t slots = 2; slots <= 3; ++slots) {
        present_image_builder_t b;
        assert(present_image_init(&b, W, H, slots) == ESP_OK);
        uint16_t pixels[3][N] = {{0}}, reference[N] = {0};
        size_t baseline = SIZE_MAX;
        unsigned overflows = 0;
        for (unsigned frame = 0; frame < 1500; ++frame) {
            size_t candidate = baseline == SIZE_MAX ? 0 : (baseline + 1 + random_u(slots - 1)) % slots;
            assert(present_image_begin(&b, candidate, baseline) == ESP_OK);
            uint16_t next[N];
            memcpy(next, reference, sizeof(next));
            unsigned writes = frame ? 1 + random_u(20) : N;
            for (unsigned j = 0; j < writes; ++j) {
                unsigned i = frame ? random_u(N) : j;
                pixels[candidate][i] = next[i] = frame + 1;
                assert(present_image_cover(&b, (esp_display_present_rect_t) {
                    i % W, i / W, 1, 1
                }) == ESP_OK);
            }
            if (frame && frame % 7 == 0) {
                present_image_cancel(&b);
                continue;
            }
            if (b.stale[candidate].full) {
                ++overflows;
            }
            present_image_prepare_repair(&b, false);
            esp_display_present_rect_t area;
            while (present_image_next_repair(&b, &area)) {
                assert(baseline != SIZE_MAX);
                for (unsigned y = area.y; y < area.y + area.height; ++y) {
                    memcpy(&pixels[candidate][y * W + area.x], &pixels[baseline][y * W + area.x], area.width * 2);
                }
                assert(present_image_mark_repaired(&b, &area) == ESP_OK);
            }
            assert(!memcmp(pixels[candidate], next, sizeof(next)));
            assert(present_image_commit(&b) == ESP_OK);
            memcpy(reference, next, sizeof(next));
            assert(!b.stale[candidate].count && !b.stale[candidate].full);
            baseline = candidate;
        }
        assert(overflows);
        present_image_deinit(&b);
    }
    present_image_builder_t no_history;
    fail_coverage_alloc = true;
    assert(present_image_init(&no_history, W, H, 0) == ESP_OK);
    assert(!no_history.stale && !no_history.history_slots);
    present_image_deinit(&no_history);
    assert(present_image_init(&no_history, W, H, 2) == ESP_ERR_NO_MEM);
    present_image_deinit(&no_history);
    fail_coverage_alloc = false;
    rng = previous_rng;
}

int main(void)
{
    seeded_bitmap_band();
    stale_slot_reference();
    lazy_coverage();
    rectangle_reference();
    band_reference();
    mid_repair_bitmap_fallback();
    seeded_baseline();
    enum { W = 17, H = 9, N = W * H };
    present_image_builder_t b;
    assert(present_image_init(&b, W, H, 3) == ESP_OK);
    uint16_t buffers[3][N], reference[N];
    memset(buffers, 0xa5, sizeof(buffers));
    memset(reference, 0, sizeof(reference));
    size_t baseline = SIZE_MAX;
    for (unsigned frame = 0; frame < 80; frame++) {
        size_t candidate = frame % 3;
        present_image_begin(&b, candidate, baseline);
        if (!frame) {
            for (unsigned i = 0; i < N; i++) {
                buffers[candidate][i] = reference[i] = i + 1;
            }
            present_image_cover(&b, (esp_display_present_rect_t) {
                0, 0, W, H
            });
            assert(present_image_full(&b));
        } else {
            unsigned x = frame % W, y = (frame / 3) % H;
            reference[y * W + x] = buffers[candidate][y * W + x] = 1000 + frame;
            present_image_cover(&b, (esp_display_present_rect_t) {
                x, y, 1, 1
            });
            assert(!present_image_full(&b));
        }
        present_image_prepare_repair(&b, false);
        esp_display_present_rect_t a;
        while (present_image_next_repair(&b, &a)) {
            assert(baseline != SIZE_MAX);
            for (unsigned y = a.y; y < a.y + a.height; y++) {
                memcpy(buffers[candidate] + y * W + a.x, buffers[baseline] + y * W + a.x, a.width * 2);
            }
            present_image_mark_repaired(&b, &a);
        }
        assert(!memcmp(buffers[candidate], reference, sizeof(reference)));
        if (frame > 2) {
            assert(b.repaired_pixels <= 2);
        }
        assert(present_image_commit(&b) == ESP_OK);
        baseline = candidate;
    }
    /* Cancel invalidates a contaminated allocation; history overflow cannot
     * turn a conservative dirty union into invented exact coverage. */
    size_t candidate = (baseline + 1) % 3;
    present_image_begin(&b, candidate, baseline);
    present_image_cover(&b, (esp_display_present_rect_t) {
        0, 0, 1, 1
    });
    present_image_cancel(&b);
    assert(!b.versions[candidate]);
    present_image_begin(&b, candidate, baseline);
    for (unsigned i = 0; i < 40; i++)
        present_image_cover(&b, (esp_display_present_rect_t) {
        (i * 2) % W, (i * 2) / W, 1, 1
    });
    assert(b.writes.full && !present_image_full(&b));
    present_image_prepare_repair(&b, false);
    esp_display_present_rect_t a;
    while (present_image_next_repair(&b, &a)) {
        present_image_mark_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 40);
    /* Adjacent horizontal bands coalesce without overflowing PRESENT_MAX_DAMAGE_AREAS. */
    present_image_cancel(&b);
    present_image_begin(&b, candidate, baseline);
    for (unsigned y = 0; y < 8; y++) {
        present_image_cover(&b, (esp_display_present_rect_t) {
            2, y, 5, 1
        });
    }
    assert(!b.writes.full && b.writes.count == 1 && b.writes.areas[0].height == 8);
    /* Missing history safely requires a full repair, minus successful writes. */
    present_image_cancel(&b);
    b.versions[candidate] = 1;
    present_image_begin(&b, candidate, baseline);
    present_image_cover(&b, (esp_display_present_rect_t) {
        3, 2, 2, 1
    });
    present_image_prepare_repair(&b, false);
    while (present_image_next_repair(&b, &a)) {
        present_image_mark_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 2);
    /* DIRECT protects against writes outside producer-declared coverage. */
    present_image_cancel(&b);
    b.versions[candidate] = b.versions[baseline];
    present_image_begin(&b, candidate, baseline);
    present_image_cover(&b, (esp_display_present_rect_t) {
        3, 2, 2, 1
    });
    present_image_prepare_repair(&b, true);
    while (present_image_next_repair(&b, &a)) {
        present_image_mark_repaired(&b, &a);
    }
    assert(b.repaired_pixels == N - 2);
    present_image_deinit(&b);
    return 0;
}
