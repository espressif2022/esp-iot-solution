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
    memset(b->coverage, 0, b->coverage_bytes);
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
    for (uint32_t y = a.y; y < a.y + a.height; y++) {
        pv2_coverage_set(b->coverage, (size_t)y * b->width + a.x, a.width);
    }
    damage_add(&b->writes, a);
}
bool pv2_image_full(const pv2_image_builder_t *b)
{
    return pv2_coverage_full(b->coverage, (size_t)b->width * b->height);
}
void pv2_image_prepare_repair(pv2_image_builder_t *b, bool direct)
{
    if (b->repair_ready) {
        return;
    }
    b->repair_ready = true;
    if (b->baseline == SIZE_MAX || b->baseline == b->candidate) {
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
        while (b->offset < count) {
            uint32_t x = a.x + b->offset % a.width, y = a.y + b->offset / a.width;
            if (covered(b, (size_t)y * b->width + x)) {
                b->offset++;
                continue;
            }
            uint32_t end = x + 1;
            while (end < a.x + a.width && !covered(b, (size_t)y * b->width + end)) {
                end++;
            }
            *out = (pv2_rect_t) {
                x, y, end - x, 1
            };
            return true;
        }
        b->rect++;
        b->offset = 0;
    }
    return false;
}
void pv2_image_repaired(pv2_image_builder_t *b, const pv2_rect_t *a)
{
    size_t count = (size_t)a->width * a->height;
    b->offset += count;
    b->repaired_pixels += count;
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
        b->versions[b->candidate] = 0;
    }
    if (b->baseline_borrow) {
        pv2_allocation_drop(&b->allocations[b->baseline], PV2_REF_COPY);
        b->baseline_borrow = false;
    }
}
