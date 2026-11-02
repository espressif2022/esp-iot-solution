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
static bool merge_adjacent(pv2_rect_t *area, pv2_rect_t b)
{
    if (area->x == b.x && area->width == b.width) {
        uint32_t ay2 = area->y + area->height, by2 = b.y + b.height;
        if (b.y <= ay2 && area->y <= by2) {
            uint32_t y1 = area->y < b.y ? area->y : b.y;
            uint32_t y2 = ay2 > by2 ? ay2 : by2;
            area->y = y1;
            area->height = y2 - y1;
            return true;
        }
    }
    if (area->y == b.y && area->height == b.height) {
        uint32_t ax2 = area->x + area->width, bx2 = b.x + b.width;
        if (b.x <= ax2 && area->x <= bx2) {
            uint32_t x1 = area->x < b.x ? area->x : b.x;
            uint32_t x2 = ax2 > bx2 ? ax2 : bx2;
            area->x = x1;
            area->width = x2 - x1;
            return true;
        }
    }
    return false;
}
static void damage_add(pv2_damage_set_t *damage, pv2_rect_t a)
{
    if (damage->full) {
        return;
    }
    for (size_t i = 0; i < damage->count; i++) {
        if (contains(damage->areas[i], a)) {
            return;
        }
        if (contains(a, damage->areas[i])) {
            damage->areas[i] = a;
            return;
        }
        if (merge_adjacent(&damage->areas[i], a)) {
            for (size_t j = 0; j < damage->count;) {
                if (j != i && (contains(damage->areas[i], damage->areas[j]) || merge_adjacent(&damage->areas[i], damage->areas[j]))) {
                    damage->areas[j] = damage->areas[--damage->count];
                    if (i == damage->count) {
                        i = j;
                    }
                    j = 0;
                } else {
                    j++;
                }
            }
            return;
        }
    }
    if (damage->count == PV2_MAX_DAMAGE_AREAS) {
        damage->full = true;
        damage->count = 0;
        return;
    }
    damage->areas[damage->count++] = a;
}
esp_err_t pv2_image_init(pv2_image_builder_t *builder, uint32_t w, uint32_t h)
{
    if (!builder || !w || !h || (size_t)w > SIZE_MAX / h) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(builder, 0, sizeof(*builder));
    size_t pixels = (size_t)w * h;
    builder->coverage_bytes = pixels / 8 + (pixels % 8 != 0);
    builder->coverage = pv2_alloc(builder->coverage_bytes);
    if (!builder->coverage) {
        return ESP_ERR_NO_MEM;
    }
    builder->width = w;
    builder->height = h;
    builder->candidate = builder->baseline = builder->published_baseline = SIZE_MAX;
    return ESP_OK;
}
void pv2_image_deinit(pv2_image_builder_t *builder)
{
    pv2_free(builder->coverage);
    builder->coverage = NULL;
}
void pv2_image_reset(pv2_image_builder_t *builder, uint32_t w, uint32_t h)
{
    assert((size_t)w * h == (size_t)builder->width * builder->height);
    if (builder->published_baseline != SIZE_MAX) {
        pv2_allocation_drop(&builder->allocations[builder->published_baseline], PV2_REF_BASELINE);
    }
    builder->published_baseline = SIZE_MAX;
    builder->width = w;
    builder->height = h;
    builder->latest = 0;
    memset(builder->versions, 0, sizeof(builder->versions));
    memset(builder->history, 0, sizeof(builder->history));
}
esp_err_t pv2_image_begin(pv2_image_builder_t *builder, size_t candidate, size_t baseline)
{
    assert(candidate < PV2_IMAGE_SLOTS && (baseline == SIZE_MAX || baseline < PV2_IMAGE_SLOTS));
    if (!pv2_allocation_write(&builder->allocations[candidate], candidate == baseline, builder->allow_scanout_writes)) {
        return ESP_ERR_INVALID_STATE;
    }
    builder->candidate = candidate;
    builder->baseline = baseline;
    builder->baseline_borrow = baseline != SIZE_MAX && baseline != candidate;
    if (builder->baseline_borrow) {
        pv2_allocation_hold(&builder->allocations[baseline], PV2_REF_COPY);
    }
    if (builder->bitmap_dirty) {
        memset(builder->coverage, 0, builder->coverage_bytes);
        builder->bitmap_dirty = false;
    }
    builder->full_covered = false;
    builder->writes = (pv2_damage_set_t) {
        0
    };
    builder->repair = (pv2_damage_set_t) {
        0
    };
    builder->rect = builder->offset = builder->repaired_pixels = 0;
    builder->repair_ready = false;
    return ESP_OK;
}
void pv2_image_cover(pv2_image_builder_t *builder, pv2_rect_t a)
{
    assert(pv2_valid_rect(builder->width, builder->height, &a));
    if (a.x == 0 && a.y == 0 && a.width == builder->width && a.height == builder->height) {
        builder->full_covered = true;
        builder->writes.full = true;
        builder->writes.count = 0;
        return;
    }
    builder->bitmap_dirty = true;
    if (a.x == 0 && a.width == builder->width) {
        pv2_coverage_set(builder->coverage, (size_t)a.y * builder->width, (size_t)a.height * builder->width);
    } else {
        for (uint32_t y = a.y; y < a.y + a.height; y++) {
            pv2_coverage_set(builder->coverage, (size_t)y * builder->width + a.x, a.width);
        }
    }
    damage_add(&builder->writes, a);
    if (!builder->writes.full && builder->writes.count == 1 &&
            builder->writes.areas[0].x == 0 && builder->writes.areas[0].y == 0 &&
            builder->writes.areas[0].width == builder->width && builder->writes.areas[0].height == builder->height) {
        builder->full_covered = true;
    }
}
bool pv2_image_full(const pv2_image_builder_t *builder)
{
    return builder->full_covered || pv2_coverage_full(builder->coverage, (size_t)builder->width * builder->height);
}
void pv2_image_prepare_repair(pv2_image_builder_t *builder, bool direct)
{
    if (builder->repair_ready) {
        return;
    }
    builder->repair_ready = true;
    if (builder->full_covered || builder->baseline == SIZE_MAX || builder->baseline == builder->candidate) {
        return;
    }
    uint64_t from = builder->versions[builder->candidate], to = builder->versions[builder->baseline];
    if (direct || !from || !to || from > to || to - from > PV2_IMAGE_HISTORY) {
        builder->repair.full = true;
    } else
        for (uint64_t v = from; v < to;) {
            ++v;
            pv2_image_delta_t *delta = &builder->history[v % PV2_IMAGE_HISTORY];
            if (delta->version != v || delta->damage.full) {
                builder->repair.full = true;
                break;
            }
            for (size_t i = 0; i < delta->damage.count; i++) {
                damage_add(&builder->repair, delta->damage.areas[i]);
            }
        }
    if (builder->repair.full) {
        builder->repair.count = 1;
        builder->repair.areas[0] = (pv2_rect_t) {
            0, 0, builder->width, builder->height
        };
    }
}
static bool covered(const pv2_image_builder_t *builder, size_t i)
{
    return (builder->coverage[i / 8] & (1u << (i % 8))) != 0;
}
bool pv2_image_next_repair(pv2_image_builder_t *builder, pv2_rect_t *out)
{
    assert(builder->repair_ready);
    while (builder->rect < builder->repair.count) {
        pv2_rect_t a = builder->repair.areas[builder->rect];
        size_t count = (size_t)a.width * a.height;
        uint32_t ry = (uint32_t)(builder->offset / a.width);
        uint32_t rx = (uint32_t)(builder->offset % a.width);
        while (ry < a.height) {
            uint32_t y = a.y + ry;
            size_t row_base = (size_t)y * builder->width;
            /* Fast skip over covered pixels on this row, 8 pixels at a time when byte-aligned. */
            while (rx < a.width) {
                size_t bit = row_base + a.x + rx;
                if (bit % 8 == 0 && rx + 8 <= a.width && builder->coverage[bit / 8] == 0xff) {
                    rx += 8;
                    continue;
                }
                if (!covered(builder, bit)) {
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
                if (bit % 8 == 0 && end + 8 <= limit && builder->coverage[bit / 8] == 0x00) {
                    end += 8;
                    continue;
                }
                if (covered(builder, bit)) {
                    break;
                }
                end++;
            }
            uint32_t span_w = end - x;
            uint32_t rows = 1;
            while (ry + rows < a.height &&
                    pv2_coverage_all_zero(builder->coverage, (size_t)(y + rows) * builder->width + x, span_w)) {
                rows++;
            }
            uint32_t next_rx = (end - a.x);
            uint32_t next_ry = ry;
            if (next_rx >= a.width) {
                next_rx = 0;
                next_ry = (rx == 0) ? (ry + rows) : (ry + 1);
            }
            builder->offset = (size_t)next_ry * a.width + next_rx;
            *out = (pv2_rect_t) {
                x, y, span_w, rows
            };
            return true;
        }
        builder->offset = count;
        builder->rect++;
        builder->offset = 0;
    }
    return false;
}
void pv2_image_repaired(pv2_image_builder_t *builder, const pv2_rect_t *area)
{
    builder->bitmap_dirty = true;
    for (uint32_t y = area->y; y < area->y + area->height; y++) {
        pv2_coverage_set(builder->coverage, (size_t)y * builder->width + area->x, area->width);
    }
    builder->repaired_pixels += (size_t)area->width * area->height;
}
esp_err_t pv2_image_commit(pv2_image_builder_t *builder)
{
    if (builder->latest == UINT64_MAX) {
        return ESP_ERR_NO_MEM;
    }
    pv2_allocation_written(&builder->allocations[builder->candidate]);
    if (builder->baseline_borrow) {
        pv2_allocation_drop(&builder->allocations[builder->baseline], PV2_REF_COPY);
        builder->baseline_borrow = false;
    }
    if (builder->published_baseline != SIZE_MAX) {
        pv2_allocation_drop(&builder->allocations[builder->published_baseline], PV2_REF_BASELINE);
    }
    pv2_allocation_hold(&builder->allocations[builder->candidate], PV2_REF_BASELINE);
    builder->published_baseline = builder->candidate;
    uint64_t v = ++builder->latest;
    builder->versions[builder->candidate] = v;
    builder->history[v % PV2_IMAGE_HISTORY] = (pv2_image_delta_t) {
        v, builder->writes
    };
    return ESP_OK;
}
void pv2_image_cancel(pv2_image_builder_t *builder)
{
    if (builder->candidate < PV2_IMAGE_SLOTS) {
        if (builder->allocations[builder->candidate].writer) {
            pv2_allocation_written(&builder->allocations[builder->candidate]);
        }
        if (builder->published_baseline == builder->candidate) {
            pv2_allocation_drop(&builder->allocations[builder->candidate], PV2_REF_BASELINE);
            builder->published_baseline = SIZE_MAX;
        }
        builder->versions[builder->candidate] = 0;
    }
    if (builder->baseline_borrow) {
        pv2_allocation_drop(&builder->allocations[builder->baseline], PV2_REF_COPY);
        builder->baseline_borrow = false;
    }
}
