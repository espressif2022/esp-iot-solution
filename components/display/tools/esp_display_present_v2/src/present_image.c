/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_image.h"
#include "present_coverage.h"
#include <string.h>
#include <assert.h>

static bool contains(pv2_rect_t a, pv2_rect_t b)
{
    return b.x >= a.x && b.y >= a.y && b.width <= a.width - (b.x - a.x) &&
           b.x - a.x <= a.width && b.y - a.y <= a.height && b.height <= a.height - (b.y - a.y);
}
static void damage_add(pv2_damage_set_t *s, pv2_rect_t a)
{
    if (s->full) {
        return;
    }
    for (size_t i = 0; i < s->count; i++) {
        if (contains(s->areas[i], a)) {
            return;
        }
        if (contains(a, s->areas[i])) {
            s->areas[i] = a;
            return;
        }
    }
    if (s->count == PV2_MAX_DAMAGE_AREAS) {
        s->full = true;
        s->count = 0;
        return;
    }
    s->areas[s->count++] = a;
}
esp_err_t pv2_image_init(pv2_image_builder_t *b, uint32_t w, uint32_t h)
{
    if (!b || !w || !h || (size_t)w > SIZE_MAX / h) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(b, 0, sizeof(*b));
    size_t pixels = (size_t)w * h;
    b->coverage_bytes = pixels / 8 + (pixels % 8 != 0);
    b->coverage = pv2_alloc(b->coverage_bytes);
    if (!b->coverage) {
        return ESP_ERR_NO_MEM;
    }
    b->width = w;
    b->height = h;
    b->candidate = b->baseline = b->published_baseline = SIZE_MAX;
    return ESP_OK;
}
void pv2_image_deinit(pv2_image_builder_t *b)
{
    pv2_free(b->coverage);
    b->coverage = NULL;
}
void pv2_image_reset(pv2_image_builder_t *b, uint32_t w, uint32_t h)
{
    assert((size_t)w * h == (size_t)b->width * b->height);
    if (b->published_baseline != SIZE_MAX) {
        pv2_allocation_drop(&b->allocations[b->published_baseline], PV2_REF_BASELINE);
    }
    b->published_baseline = SIZE_MAX;
    b->width = w;
    b->height = h;
    b->latest = 0;
    memset(b->versions, 0, sizeof(b->versions));
    memset(b->history, 0, sizeof(b->history));
}
esp_err_t pv2_image_begin(pv2_image_builder_t *b, size_t candidate, size_t baseline)
{
    assert(candidate < PV2_IMAGE_SLOTS && (baseline == SIZE_MAX || baseline < PV2_IMAGE_SLOTS));
    if (!pv2_allocation_write(&b->allocations[candidate], candidate == baseline, b->allow_scanout_writes)) {
        return ESP_ERR_INVALID_STATE;
    }
    b->candidate = candidate;
    b->baseline = baseline;
    b->baseline_borrow = baseline != SIZE_MAX && baseline != candidate;
    if (b->baseline_borrow) {
        pv2_allocation_hold(&b->allocations[baseline], PV2_REF_COPY);
    }
    if (b->bitmap_dirty) {
        memset(b->coverage, 0, b->coverage_bytes);
        b->bitmap_dirty = false;
    }
    b->full_covered = false;
    b->writes = (pv2_damage_set_t) {
        0
    };
    b->repair = (pv2_damage_set_t) {
        0
    };
    b->rect = b->offset = b->repaired_pixels = 0;
    b->repair_ready = false;
    return ESP_OK;
}
void pv2_image_cover(pv2_image_builder_t *b, pv2_rect_t a)
{
    assert(pv2_valid_rect(b->width, b->height, &a));
    if (a.x == 0 && a.y == 0 && a.width == b->width && a.height == b->height) {
        b->full_covered = true;
        b->writes.full = true;
        b->writes.count = 0;
        return;
    }
    b->bitmap_dirty = true;
    if (a.x == 0 && a.width == b->width) {
        pv2_coverage_set(b->coverage, (size_t)a.y * b->width, (size_t)a.height * b->width);
    } else {
        for (uint32_t y = a.y; y < a.y + a.height; y++) {
            pv2_coverage_set(b->coverage, (size_t)y * b->width + a.x, a.width);
        }
    }
    damage_add(&b->writes, a);
}
bool pv2_image_full(const pv2_image_builder_t *b)
{
    return b->full_covered || pv2_coverage_full(b->coverage, (size_t)b->width * b->height);
}
void pv2_image_prepare_repair(pv2_image_builder_t *b, bool direct)
{
    if (b->repair_ready) {
        return;
    }
    b->repair_ready = true;
    if (b->full_covered || b->baseline == SIZE_MAX || b->baseline == b->candidate) {
        return;
    }
    uint64_t from = b->versions[b->candidate], to = b->versions[b->baseline];
    if (direct || !from || !to || from > to || to - from > PV2_IMAGE_HISTORY) {
        b->repair.full = true;
    } else
        for (uint64_t v = from; v < to;) {
            ++v;
            pv2_image_delta_t *d = &b->history[v % PV2_IMAGE_HISTORY];
            if (d->version != v || d->damage.full) {
                b->repair.full = true;
                break;
            }
            for (size_t i = 0; i < d->damage.count; i++) {
                damage_add(&b->repair, d->damage.areas[i]);
            }
        }
    if (b->repair.full) {
        b->repair.count = 1;
        b->repair.areas[0] = (pv2_rect_t) {
            0, 0, b->width, b->height
        };
    }
}
static bool covered(const pv2_image_builder_t *b, size_t i)
{
    return (b->coverage[i / 8] & (1u << (i % 8))) != 0;
}
bool pv2_image_next_repair(pv2_image_builder_t *b, pv2_rect_t *out)
{
    assert(b->repair_ready);
    while (b->rect < b->repair.count) {
        pv2_rect_t a = b->repair.areas[b->rect];
        size_t count = (size_t)a.width * a.height;
        uint32_t ry = (uint32_t)(b->offset / a.width);
        uint32_t rx = (uint32_t)(b->offset % a.width);
        while (ry < a.height) {
            uint32_t y = a.y + ry;
            size_t row_base = (size_t)y * b->width;
            /* Fast skip over covered pixels on this row, 8 pixels at a time when byte-aligned. */
            while (rx < a.width) {
                size_t bit = row_base + a.x + rx;
                if (bit % 8 == 0 && rx + 8 <= a.width && b->coverage[bit / 8] == 0xff) {
                    rx += 8;
                    continue;
                }
                if (!covered(b, bit)) {
                    break;
                }
                rx++;
            }
            if (rx >= a.width) {
                ry++;
                rx = 0;
                continue;
            }
            uint32_t x = a.x + rx;
            uint32_t end = x + 1;
            uint32_t limit = a.x + a.width;
            while (end < limit) {
                size_t bit = row_base + end;
                if (bit % 8 == 0 && end + 8 <= limit && b->coverage[bit / 8] == 0x00) {
                    end += 8;
                    continue;
                }
                if (covered(b, bit)) {
                    break;
                }
                end++;
            }
            uint32_t span_w = end - x;
            uint32_t rows = 1;
            while (ry + rows < a.height &&
                    pv2_coverage_all_zero(b->coverage, (size_t)(y + rows) * b->width + x, span_w)) {
                rows++;
            }
            b->offset = (size_t)ry * a.width + rx;
            *out = (pv2_rect_t) {
                x, y, span_w, rows
            };
            return true;
        }
        b->offset = count;
        b->rect++;
        b->offset = 0;
    }
    return false;
}
void pv2_image_repaired(pv2_image_builder_t *b, const pv2_rect_t *a)
{
    b->bitmap_dirty = true;
    for (uint32_t y = a->y; y < a->y + a->height; y++) {
        pv2_coverage_set(b->coverage, (size_t)y * b->width + a->x, a->width);
    }
    b->repaired_pixels += (size_t)a->width * a->height;
}
esp_err_t pv2_image_commit(pv2_image_builder_t *b)
{
    if (b->latest == UINT64_MAX) {
        return ESP_ERR_NO_MEM;
    }
    pv2_allocation_written(&b->allocations[b->candidate]);
    if (b->baseline_borrow) {
        pv2_allocation_drop(&b->allocations[b->baseline], PV2_REF_COPY);
        b->baseline_borrow = false;
    }
    if (b->published_baseline != SIZE_MAX) {
        pv2_allocation_drop(&b->allocations[b->published_baseline], PV2_REF_BASELINE);
    }
    pv2_allocation_hold(&b->allocations[b->candidate], PV2_REF_BASELINE);
    b->published_baseline = b->candidate;
    uint64_t v = ++b->latest;
    b->versions[b->candidate] = v;
    b->history[v % PV2_IMAGE_HISTORY] = (pv2_image_delta_t) {
        v, b->writes
    };
    return ESP_OK;
}
void pv2_image_cancel(pv2_image_builder_t *b)
{
    if (b->candidate < PV2_IMAGE_SLOTS) {
        if (b->allocations[b->candidate].writer) {
            pv2_allocation_written(&b->allocations[b->candidate]);
        }
        if (b->published_baseline == b->candidate) {
            pv2_allocation_drop(&b->allocations[b->candidate], PV2_REF_BASELINE);
            b->published_baseline = SIZE_MAX;
        }
        b->versions[b->candidate] = 0;
    }
    if (b->baseline_borrow) {
        pv2_allocation_drop(&b->allocations[b->baseline], PV2_REF_COPY);
        b->baseline_borrow = false;
    }
}
