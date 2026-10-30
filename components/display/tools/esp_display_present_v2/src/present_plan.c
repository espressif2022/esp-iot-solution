/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_plan.h"
#include "present_config.h"
#include "present_limits.h"
#include "present_memory.h"
#include <limits.h>
#include <string.h>

static size_t gcd(size_t a, size_t b)
{
    while (b) {
        size_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}
static bool mul(size_t a, size_t b, size_t *out)
{
    if (b && a > SIZE_MAX / b) {
        return false;
    }
    *out = a * b;
    return true;
}
static bool add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}
static bool lcm(size_t a, size_t b, size_t *out)
{
    a = a ? a : 1;
    b = b ? b : 1;
    return mul(a / gcd(a, b), b, out);
}
static bool align_up(size_t value, size_t alignment, size_t *out)
{
    alignment = alignment ? alignment : 1;
    size_t rem = value % alignment;
    return add(value, rem ? alignment - rem : 0, out);
}
static bool power_two(size_t x)
{
    return x && !(x & (x - 1));
}
static bool layout(uint32_t w, uint32_t h, size_t bpp, size_t stride_align,
                   pv2_pixel_format_t format, pv2_byte_order_t order, pv2_layout_t *out)
{
    size_t row, alignment;
    if (!mul(w, bpp, &row) || !lcm(bpp, stride_align, &alignment) || !align_up(row, alignment, &row)) {
        return false;
    }
    *out = (pv2_layout_t) {
        w, h, row, format, order
    };
    return true;
}
static bool pool(pv2_plan_pool_t *p, size_t n, pv2_layout_t l, size_t a, pv2_plan_memory_t memory, bool borrowed)
{
    *p = (pv2_plan_pool_t) {
        .count = n, .layout = l, .memory = memory, .borrowed = borrowed
    };
    if (!n) {
        return true;
    }
    size_t span;
    return mul(l.stride_bytes, l.height, &span) && align_up(span, a, &p->bytes_each) && mul(n, p->bytes_each, &p->total_bytes);
}
static bool executor_supports(const pv2_executor_caps_t *c, const pv2_backend_config_t *b)
{
    return (c->format_mask & (1u << b->format)) && (c->rotation_mask & (1u << b->rotation)) &&
           (b->draw_order == b->panel_order || c->byte_swap);
}
static bool valid_memory(pv2_plan_memory_t m)
{
    return m == PV2_MEMORY_INTERNAL_DMA || m == PV2_MEMORY_EXTERNAL;
}

static esp_err_t resolve(const pv2_plan_config_t *c, pv2_resolved_plan_t *p)
{
    if (!c || c->defaults < PV2_PLAN_V2_DEFAULTS || c->defaults > PV2_PLAN_V1_DEFAULTS ||
            !valid_memory(c->resources.tile_memory) || !valid_memory(c->resources.image_memory) || !valid_memory(c->resources.logical_memory)) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_config_t in = c->presenter;
    if (c->defaults == PV2_PLAN_V1_DEFAULTS && in.mode == PV2_MODE_AUTO) {
        in.mode = in.backend_kind == PV2_PRESENTER_FB ? PV2_MODE_TRIPLE_PARTIAL : c->te_enabled ? PV2_MODE_TE_SYNC : PV2_MODE_NONE;
    }
    esp_err_t e = pv2_config_resolve_legacy(&in, &p->normalized);
    if (e != ESP_OK) {
        return e;
    }
    pv2_backend_config_t *b = &p->normalized.backend;
    pv2_presenter_backend_t kind = p->normalized.backend_kind;
    if (!b->width || !b->height || b->width > INT_MAX || b->height > INT_MAX ||
            b->rotation < PV2_ROTATE_0 || b->rotation > PV2_ROTATE_270 ||
            b->format < PV2_FORMAT_RGB565 || b->format > PV2_FORMAT_RGB888 ||
            b->draw_order < PV2_ORDER_NATIVE || b->draw_order > PV2_ORDER_SWAPPED ||
            b->panel_order < PV2_ORDER_NATIVE || b->panel_order > PV2_ORDER_SWAPPED ||
            (b->format == PV2_FORMAT_RGB888 && (b->draw_order || b->panel_order)) ||
            b->render_contract < PV2_CONTRACT_PARTITION || b->render_contract > PV2_CONTRACT_DIRECT ||
            !b->max_frames || b->max_frames > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    bool partial = b->render_contract == PV2_CONTRACT_PARTITION;
    if (kind != PV2_PRESENTER_FB && !partial) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (c->defaults == PV2_PLAN_V1_DEFAULTS && b->rotation != PV2_ROTATE_0 &&
            (kind == PV2_PRESENTER_GRAM || b->render_contract == PV2_CONTRACT_DIRECT)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (b->in_place_convert && (kind != PV2_PRESENTER_GRAM || b->async_copy || b->rotation != PV2_ROTATE_0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((b->copy_execute && !b->async_copy) ||
            (p->normalized.background_service && kind == PV2_PRESENTER_GRAM && !b->async_copy)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kind != PV2_PRESENTER_FB && (b->external_buffers || b->external_buffer_bytes || b->initial_scanout_buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((!b->external_buffers && (b->external_buffer_bytes || b->initial_scanout_buffer)) ||
            (c->output.external_buffers_required && !b->external_buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t images = kind == PV2_PRESENTER_FB ? b->buffer_count : kind == PV2_PRESENTER_TE ?
                    (c->defaults == PV2_PLAN_V1_DEFAULTS ? 1 : 2) : 0;
    if (c->resources.image_count) {
        if (kind == PV2_PRESENTER_GRAM || (kind == PV2_PRESENTER_FB && in.mode != PV2_MODE_DEFAULT && c->resources.image_count != images)) {
            return ESP_ERR_INVALID_ARG;
        }
        images = c->resources.image_count;
    }
    if ((kind == PV2_PRESENTER_TE && (images < 1 || images > 2)) ||
            (kind == PV2_PRESENTER_FB && (images < 1 || images > 3))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kind == PV2_PRESENTER_FB && images == 1 && !b->allow_active_scanout_writes) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t tiles = partial ? (c->resources.tile_count ? c->resources.tile_count :
                              kind == PV2_PRESENTER_FB ? 2 : b->buffer_count ? b->buffer_count : 2) : 0;
    if ((!partial && c->resources.tile_count) || tiles > PV2_MAX_TILE_BUFFERS) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = b->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (partial && !b->tile_rows) {
        size_t row;
        if (!mul(b->width, bpp, &row)) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t rows = 32768 / row;
        b->tile_rows = rows ? (rows < b->height ? rows : b->height) : 1;
    }
    if (partial && b->tile_rows > b->height) {
        return ESP_ERR_INVALID_ARG;
    }
    p->output = c->output;
    if (!p->output.x_alignment) {
        p->output.x_alignment = 1;
    }
    if (!p->output.y_alignment) {
        p->output.y_alignment = 1;
    }
    if (!p->output.width_alignment) {
        p->output.width_alignment = 1;
    }
    if (!p->output.height_alignment) {
        p->output.height_alignment = 1;
    }
    if (!p->output.base_alignment) {
        p->output.base_alignment = PV2_BUFFER_ALIGNMENT;
    }
    if (!p->output.stride_alignment) {
        p->output.stride_alignment = 1;
    }
    if (!power_two(p->output.base_alignment)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!p->output.internal_dma && !p->output.external_memory) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p->output.full_frame_only && kind == PV2_PRESENTER_GRAM) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool external_tx = (images ? c->resources.image_memory : c->resources.tile_memory) == PV2_MEMORY_EXTERNAL;
    size_t length_alignment = p->output.external_length_alignment ? p->output.external_length_alignment : 1;
    if (external_tx && kind == PV2_PRESENTER_GRAM) {
        /* Tight rows with this width make every rendered band DMA-legal,
         * including the last short band. Negotiate before rasterization. */
        size_t width_alignment;
        if (!lcm(p->output.width_alignment, length_alignment / gcd(length_alignment, bpp), &width_alignment) || width_alignment > UINT32_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        p->output.width_alignment = width_alignment;
    }
    if (kind == PV2_PRESENTER_GRAM) {
        size_t wa, ha;
        if (!lcm(p->output.x_alignment, p->output.width_alignment, &wa) ||
                !lcm(p->output.y_alignment, p->output.height_alignment, &ha) || wa > UINT32_MAX || ha > UINT32_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        p->output.width_alignment = wa;
        p->output.height_alignment = ha;
    }
    pv2_executor_caps_t caps = {.base_alignment = 1, .destination_capacity_alignment = 1, .stride_alignment_bytes = 1};
    p->executor_reason = PV2_EXEC_SUPPORTED;
    if (c->executor) {
        if (!b->async_copy || b->copy_execute) {
            return ESP_ERR_INVALID_ARG;
        }
        e = pv2_executor_get_caps(c->executor->primary, &caps);
        if (e != ESP_OK) {
            return e;
        }
        p->preferred_executor = c->executor->primary;
        if (!executor_supports(&caps, b)) {
            p->executor_reason = PV2_EXEC_TRANSFORM;
            if (!c->executor->fallback) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            e = pv2_executor_get_caps(c->executor->fallback, &caps);
            if (e != ESP_OK) {
                return e;
            }
            if (!executor_supports(&caps, b)) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            p->preferred_executor = c->executor->fallback;
        }
        p->address_probe_required = true;
        b->copy_execute = pv2_executor_dispatch_execute;
        b->copy_user = (void *)c->executor;
    }
    size_t alignment;
    if (!lcm(p->output.base_alignment, PV2_BUFFER_ALIGNMENT, &alignment) ||
            !lcm(alignment, caps.base_alignment, &alignment) || !lcm(alignment, caps.destination_capacity_alignment, &alignment)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!power_two(alignment)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    p->base_alignment = alignment;
    size_t stride_align;
    if (!lcm(p->output.stride_alignment, caps.stride_alignment_bytes, &stride_align)) {
        return ESP_ERR_INVALID_SIZE;
    }
    bool quarter = b->rotation == PV2_ROTATE_90 || b->rotation == PV2_ROTATE_270;
    uint32_t pw = quarter ? b->height : b->width, ph = quarter ? b->width : b->height;
    if (!layout(b->width, b->height, bpp, caps.stride_alignment_bytes, b->format, b->draw_order, &p->logical_layout) ||
            !layout(pw, ph, bpp, stride_align, b->format, b->panel_order, &p->physical_layout)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (external_tx && images) {
        size_t transfer_bytes;
        if (!mul(p->physical_layout.stride_bytes, ph, &transfer_bytes)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (transfer_bytes % length_alignment) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (p->output.tight_stride && p->physical_layout.stride_bytes != (size_t)pw * bpp) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (pw % p->output.width_alignment || ph % p->output.height_alignment) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((caps.max_stride_pixels && (p->logical_layout.stride_bytes / bpp > caps.max_stride_pixels || p->physical_layout.stride_bytes / bpp > caps.max_stride_pixels)) ||
            (caps.max_height && (b->height > caps.max_height || ph > caps.max_height))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    size_t logical = 0;
    if (kind == PV2_PRESENTER_GRAM) {
        p->topology = PV2_TOPOLOGY_REGION;
    } else if (partial) {
        p->topology = PV2_TOPOLOGY_TILE_TO_PHYSICAL;
    } else if (b->rotation != PV2_ROTATE_0 || b->draw_order != b->panel_order) {
        p->topology = PV2_TOPOLOGY_LOGICAL_TO_PHYSICAL;
        logical = c->resources.logical_count ? c->resources.logical_count : 1;
    } else {
        p->topology = PV2_TOPOLOGY_DIRECT_SURFACE;
    }
    if ((!logical && c->resources.logical_count) || logical > b->max_frames) {
        return ESP_ERR_INVALID_ARG;
    }
    if (logical > 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (kind == PV2_PRESENTER_GRAM && b->tile_rows < (quarter ? p->output.width_alignment : p->output.height_alignment)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    pv2_layout_t tile = p->logical_layout;
    tile.height = partial ? b->tile_rows : 0;
    if (!pool(&p->tiles, tiles, tile, alignment, c->resources.tile_memory, false) ||
            !pool(&p->images, images, p->physical_layout, alignment, c->resources.image_memory, b->external_buffers != NULL) ||
            !pool(&p->logical, logical, p->logical_layout, alignment, c->resources.logical_memory, false)) {
        return ESP_ERR_INVALID_SIZE;
    }
    bool convert = kind == PV2_PRESENTER_GRAM && (b->async_copy || b->rotation != PV2_ROTATE_0 || (b->draw_order != b->panel_order && !b->in_place_convert));
    pv2_layout_t converted = tile;
    converted.byte_order = b->panel_order;
    if (quarter) {
        if (!layout(tile.height, tile.width, bpp, stride_align, b->format, b->panel_order, &converted)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (!pool(&p->conversion, convert ? tiles : 0, converted, alignment, c->resources.tile_memory, false)) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_plan_memory_t tx_memory = images ? c->resources.image_memory : c->resources.tile_memory;
    if ((tx_memory == PV2_MEMORY_INTERNAL_DMA && !p->output.internal_dma) || (tx_memory == PV2_MEMORY_EXTERNAL && !p->output.external_memory)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    p->tx_capacity = c->resources.tx_capacity ? c->resources.tx_capacity : kind == PV2_PRESENTER_GRAM ? tiles : 1;
    if (!c->resources.tx_capacity && p->output.max_inflight && p->tx_capacity > p->output.max_inflight) {
        p->tx_capacity = p->output.max_inflight;
    }
    if (!p->tx_capacity || p->tx_capacity > PV2_MAX_TILE_BUFFERS || (kind != PV2_PRESENTER_GRAM && p->tx_capacity != 1) ||
            (p->output.max_inflight && p->tx_capacity > p->output.max_inflight)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    p->record_capacity = b->max_frames;
    pv2_plan_pool_t *pools[] = {&p->tiles, &p->images, &p->logical, &p->conversion};
    for (size_t i = 0; i < 4; i++) {
        if (!add(p->pixel_bytes, pools[i]->total_bytes, &p->pixel_bytes)) {
            return ESP_ERR_INVALID_SIZE;
        }

    }
    size_t pixels;
    if (!mul(b->width, b->height, &pixels)) {
        return ESP_ERR_INVALID_SIZE;
    }
    p->coverage_bytes = images ? pixels / 8 + (pixels % 8 != 0) : 0;
    if (c->pixel_budget_bytes && p->pixel_bytes > c->pixel_budget_bytes) {
        return ESP_ERR_NO_MEM;
    }
    if (kind == PV2_PRESENTER_FB) {
        b->tile_buffer_count = tiles;
        if (b->external_buffers && images != b->buffer_count) {
            return ESP_ERR_INVALID_ARG;
        }
        b->buffer_count = images;
    } else {
        b->buffer_count = tiles;
        if (kind == PV2_PRESENTER_TE) {
            b->compose_count = images;
        }
    }
    if (b->external_buffers) {
        bool found = b->initial_scanout_buffer == NULL;
        if (b->external_buffer_bytes < p->images.bytes_each) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < images; i++) {
            uintptr_t base = (uintptr_t)b->external_buffers[i];
            if (!base || base % alignment || base > UINTPTR_MAX - b->external_buffer_bytes) {
                return ESP_ERR_INVALID_ARG;
            }
            if (b->external_buffers[i] == b->initial_scanout_buffer) {
                found = true;
            }
            for (size_t j = 0; j < i; j++) {
                uintptr_t other = (uintptr_t)b->external_buffers[j];
                if (base < other + b->external_buffer_bytes && other < base + b->external_buffer_bytes) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
        if (!found) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (kind == PV2_PRESENTER_TE && b->te_timing.period_us &&
            pv2_te_window(&b->te_timing, b->te_transfer_budget_us, 0, 0, false).admission == PV2_TE_UNSUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (kind == PV2_PRESENTER_TE && !b->te_timing.period_us && (!b->te_transfer_budget_us || b->te_transfer_budget_us >= b->te_window_us)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (p->physical_layout.stride_bytes != (size_t)pw * bpp || p->logical_layout.stride_bytes != (size_t)b->width * bpp || alignment != PV2_BUFFER_ALIGNMENT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    b->tile_in_psram = c->resources.tile_memory == PV2_MEMORY_EXTERNAL;
    b->image_in_psram = c->resources.image_memory == PV2_MEMORY_EXTERNAL;
    b->logical_in_psram = c->resources.logical_memory == PV2_MEMORY_EXTERNAL;
    b->transfer_capacity = p->tx_capacity;
    p->normalized.output_caps = p->output;
    p->metadata_bytes = pv2_presenter_memory(&p->normalized) +
                        (kind == PV2_PRESENTER_GRAM ? pv2_gram_memory(b) : kind == PV2_PRESENTER_TE ? pv2_te_memory(b) : pv2_fb_memory(b));
    p->stack_bytes = b->async_copy ? 4096 : 0;
    if (p->normalized.background_service && !add(p->stack_bytes,
                                                 p->normalized.worker_stack_bytes ? p->normalized.worker_stack_bytes : 4096, &p->stack_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    p->reserved_bytes = c->additional_budget_bytes;
    if (!add(p->pixel_bytes, p->coverage_bytes, &p->required_bytes) ||
            !add(p->required_bytes, p->metadata_bytes, &p->required_bytes) ||
            !add(p->required_bytes, p->stack_bytes, &p->required_bytes) ||
            !add(p->required_bytes, p->reserved_bytes, &p->required_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (c->total_budget_bytes && p->required_bytes > c->total_budget_bytes) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t pv2_plan_resolve(const pv2_plan_config_t *config, pv2_resolved_plan_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    pv2_resolved_plan_t p = {0};
    esp_err_t e = resolve(config, &p);
    if (e == ESP_OK) {
        *out = p;
    }
    return e;
}
static pv2_rect_t rotate(pv2_rect_t a, uint32_t w, uint32_t h, pv2_rotation_t r)
{
    switch (r) {
    case PV2_ROTATE_90:
        return (pv2_rect_t) {
            h - a.y - a.height, a.x, a.height, a.width
        };
    case PV2_ROTATE_180:
        return (pv2_rect_t) {
            w - a.x - a.width, h - a.y - a.height, a.width, a.height
        };
    case PV2_ROTATE_270:
        return (pv2_rect_t) {
            a.y, w - a.x - a.width, a.height, a.width
        };
    default:
        return a;
    }
}
esp_err_t pv2_plan_render_area(const pv2_resolved_plan_t *p, const pv2_rect_t *dirty, pv2_rect_t *render, pv2_rect_t *physical)
{
    if (!p || !dirty || !render || !physical || !dirty->width || !dirty->height ||
            dirty->x > p->logical_layout.width || dirty->y > p->logical_layout.height ||
            dirty->width > p->logical_layout.width - dirty->x || dirty->height > p->logical_layout.height - dirty->y ||
            !p->output.x_alignment || !p->output.y_alignment || !p->output.width_alignment || !p->output.height_alignment) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t a = rotate(*dirty, p->logical_layout.width, p->logical_layout.height, p->normalized.backend.rotation);
    if (p->normalized.backend.render_contract == PV2_CONTRACT_FULL) {
        a = (pv2_rect_t) {
            0, 0, p->physical_layout.width, p->physical_layout.height
        };
    } else if (p->images.count) {
        /* Full-frame output alignment is already satisfied by the candidate;
         * do not force a PARTIAL producer to repaint that entire image. */
        *physical = a;
        *render = *dirty;
        return ESP_OK;
    } else {
        size_t x = a.x - a.x % p->output.x_alignment, y = a.y - a.y % p->output.y_alignment;
        size_t w, h, wa, ha;
        if (!lcm(p->output.x_alignment, p->output.width_alignment, &wa) ||
                !lcm(p->output.y_alignment, p->output.height_alignment, &ha) ||
                !align_up((size_t)a.x + a.width - x, wa, &w) || !align_up((size_t)a.y + a.height - y, ha, &h)) {
            return ESP_ERR_INVALID_SIZE;
        }
        /* A shifted aligned origin can run past the edge even when the whole
         * axis is legal. Conservatively expand to that full axis, never clip. */
        if (w > p->physical_layout.width - x) {
            x = 0;
            w = p->physical_layout.width;
        }
        if (h > p->physical_layout.height - y) {
            y = 0;
            h = p->physical_layout.height;
        }
        if (w % wa || h % ha) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        a = (pv2_rect_t) {
            x, y, w, h
        };
    }
    *physical = a;
    pv2_rotation_t inverse = (4 - p->normalized.backend.rotation) % 4;
    *render = rotate(a, p->physical_layout.width, p->physical_layout.height, inverse);
    return ESP_OK;
}
esp_err_t pv2_presenter_create_planned(const pv2_plan_config_t *c, pv2_presenter_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    pv2_resolved_plan_t p;
    esp_err_t e = pv2_plan_resolve(c, &p);
    if (e != ESP_OK) {
        return e;
    }
    if (p.pending_runtime) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* DEFAULT prevents applying the named preset a second time. */
    p.normalized.mode = PV2_MODE_DEFAULT;
    return pv2_presenter_create(&p.normalized, out);
}
