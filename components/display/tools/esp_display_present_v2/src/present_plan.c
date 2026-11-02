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

static size_t gcd(size_t a, size_t backend_config)
{
    while (backend_config) {
        size_t t = a % backend_config;
        a = backend_config;
        backend_config = t;
    }
    return a;
}
static bool mul(size_t a, size_t backend_config, size_t *out)
{
    if (backend_config && a > SIZE_MAX / backend_config) {
        return false;
    }
    *out = a * backend_config;
    return true;
}
static bool add(size_t a, size_t backend_config, size_t *out)
{
    if (a > SIZE_MAX - backend_config) {
        return false;
    }
    *out = a + backend_config;
    return true;
}
static bool lcm(size_t a, size_t backend_config, size_t *out)
{
    a = a ? a : 1;
    backend_config = backend_config ? backend_config : 1;
    return mul(a / gcd(a, backend_config), backend_config, out);
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
static bool pool(pv2_plan_pool_t *pool_config, size_t n, pv2_layout_t l, size_t a, pv2_plan_memory_t memory, bool borrowed)
{
    *pool_config = (pv2_plan_pool_t) {
        .count = n, .layout = l, .memory = memory, .borrowed = borrowed
    };
    if (!n) {
        return true;
    }
    size_t span;
    if (!mul(l.stride_bytes, l.height, &span)) {
        return false;
    }
    if (borrowed) {
        /* Base alignment does not grant ownership of tail cache padding. */
        pool_config->bytes_each = span;
    } else if (!align_up(span, a, &pool_config->bytes_each)) {
        return false;
    }
    return mul(n, pool_config->bytes_each, &pool_config->total_bytes);
}
static bool executor_supports(const pv2_executor_caps_t *caps, const pv2_backend_config_t *config)
{
    return (caps->format_mask & (1u << config->format)) && (caps->rotation_mask & (1u << config->rotation)) &&
           (config->draw_order == config->panel_order || caps->byte_swap);
}
static bool valid_memory(pv2_plan_memory_t m)
{
    return m == PV2_MEMORY_INTERNAL_DMA || m == PV2_MEMORY_EXTERNAL;
}

static esp_err_t resolve(const pv2_plan_config_t *config, pv2_resolved_plan_t *plan)
{
    if (!config || config->defaults < PV2_PLAN_V2_DEFAULTS || config->defaults > PV2_PLAN_V1_DEFAULTS ||
            !valid_memory(config->resources.tile_memory) || !valid_memory(config->resources.image_memory) || !valid_memory(config->resources.logical_memory)) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_config_t in = config->presenter;
    if (config->defaults == PV2_PLAN_V1_DEFAULTS && in.mode == PV2_MODE_AUTO) {
        in.mode = in.backend_kind == PV2_PRESENTER_FB ? PV2_MODE_TRIPLE_PARTIAL : config->te_enabled ? PV2_MODE_TE_SYNC : PV2_MODE_NONE;
    }
    esp_err_t e = pv2_config_resolve_legacy(&in, &plan->normalized);
    if (e != ESP_OK) {
        return e;
    }
    pv2_backend_config_t *backend_config = &plan->normalized.backend;
    pv2_presenter_backend_t kind = plan->normalized.backend_kind;
    if (!backend_config->width || !backend_config->height || backend_config->width > INT_MAX || backend_config->height > INT_MAX ||
            backend_config->rotation < PV2_ROTATE_0 || backend_config->rotation > PV2_ROTATE_270 ||
            backend_config->format < PV2_FORMAT_RGB565 || backend_config->format > PV2_FORMAT_RGB888 ||
            backend_config->draw_order < PV2_ORDER_NATIVE || backend_config->draw_order > PV2_ORDER_SWAPPED ||
            backend_config->panel_order < PV2_ORDER_NATIVE || backend_config->panel_order > PV2_ORDER_SWAPPED ||
            (backend_config->format == PV2_FORMAT_RGB888 && (backend_config->draw_order || backend_config->panel_order)) ||
            backend_config->render_contract < PV2_CONTRACT_PARTITION || backend_config->render_contract > PV2_CONTRACT_DIRECT ||
            !backend_config->max_frames || backend_config->max_frames > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    bool partial = backend_config->render_contract == PV2_CONTRACT_PARTITION;
    if (kind != PV2_PRESENTER_FB && !partial) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (backend_config->in_place_convert && (kind != PV2_PRESENTER_GRAM || backend_config->async_copy || backend_config->rotation != PV2_ROTATE_0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((backend_config->copy_execute && !backend_config->async_copy) ||
            (plan->normalized.background_service && kind == PV2_PRESENTER_GRAM && !backend_config->async_copy)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kind != PV2_PRESENTER_FB && (backend_config->external_buffers || backend_config->external_buffer_bytes || backend_config->initial_scanout_buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((!backend_config->external_buffers && (backend_config->external_buffer_bytes || backend_config->initial_scanout_buffer)) ||
            (config->output.external_buffers_required && !backend_config->external_buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t images = kind == PV2_PRESENTER_FB ? backend_config->buffer_count : kind == PV2_PRESENTER_TE ?
                    (config->defaults == PV2_PLAN_V1_DEFAULTS ? 1 : 2) : 0;
    if (config->resources.image_count) {
        if (kind == PV2_PRESENTER_GRAM || (kind == PV2_PRESENTER_FB && in.mode != PV2_MODE_DEFAULT && config->resources.image_count != images)) {
            return ESP_ERR_INVALID_ARG;
        }
        images = config->resources.image_count;
    }
    if ((kind == PV2_PRESENTER_TE && (images < 1 || images > 2)) ||
            (kind == PV2_PRESENTER_FB && (images < 1 || images > 3))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (kind == PV2_PRESENTER_FB && images == 1 && !backend_config->allow_active_scanout_writes) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t tiles = partial ? (config->resources.tile_count ? config->resources.tile_count :
                              kind == PV2_PRESENTER_FB ? 2 : backend_config->buffer_count ? backend_config->buffer_count : 2) : 0;
    if ((!partial && config->resources.tile_count) || tiles > PV2_MAX_TILE_BUFFERS) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = backend_config->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (partial && !backend_config->tile_rows) {
        size_t row;
        if (!mul(backend_config->width, bpp, &row)) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t rows = 32768 / row;
        backend_config->tile_rows = rows ? (rows < backend_config->height ? rows : backend_config->height) : 1;
    }
    if (partial && backend_config->tile_rows > backend_config->height) {
        return ESP_ERR_INVALID_ARG;
    }
    plan->output = config->output;
    if (!plan->output.x_alignment) {
        plan->output.x_alignment = 1;
    }
    if (!plan->output.y_alignment) {
        plan->output.y_alignment = 1;
    }
    if (!plan->output.width_alignment) {
        plan->output.width_alignment = 1;
    }
    if (!plan->output.height_alignment) {
        plan->output.height_alignment = 1;
    }
    if (!plan->output.base_alignment) {
        plan->output.base_alignment = PV2_BUFFER_ALIGNMENT;
    }
    if (!plan->output.stride_alignment) {
        plan->output.stride_alignment = 1;
    }
    if (!power_two(plan->output.base_alignment)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!plan->output.internal_dma && !plan->output.external_memory) {
        return ESP_ERR_INVALID_ARG;
    }
    if (plan->output.full_frame_only && kind == PV2_PRESENTER_GRAM) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (backend_config->internal_tx_staging && (kind != PV2_PRESENTER_GRAM || !backend_config->async_copy)) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_plan_memory_t transport_memory = images ? config->resources.image_memory :
                                         backend_config->internal_tx_staging ? PV2_MEMORY_INTERNAL_DMA : config->resources.tile_memory;
    bool external_tx = transport_memory == PV2_MEMORY_EXTERNAL;
    size_t length_alignment = plan->output.external_length_alignment ? plan->output.external_length_alignment : 1;
    if (external_tx && kind == PV2_PRESENTER_GRAM) {
        /* Tight rows with this width make every rendered band DMA-legal,
         * including the last short band. Negotiate before rasterization. */
        size_t width_alignment;
        if (!lcm(plan->output.width_alignment, length_alignment / gcd(length_alignment, bpp), &width_alignment) || width_alignment > UINT32_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        plan->output.width_alignment = width_alignment;
    }
    if (kind == PV2_PRESENTER_GRAM) {
        size_t wa, ha;
        if (!lcm(plan->output.x_alignment, plan->output.width_alignment, &wa) ||
                !lcm(plan->output.y_alignment, plan->output.height_alignment, &ha) || wa > UINT32_MAX || ha > UINT32_MAX) {
            return ESP_ERR_INVALID_SIZE;
        }
        plan->output.width_alignment = wa;
        plan->output.height_alignment = ha;
    }
    pv2_executor_caps_t caps = {.base_alignment = 1, .destination_capacity_alignment = 1, .stride_alignment_bytes = 1};
    plan->executor_reason = PV2_EXEC_SUPPORTED;
    if (config->executor) {
        if (!backend_config->async_copy || backend_config->copy_execute) {
            return ESP_ERR_INVALID_ARG;
        }
        e = pv2_executor_get_caps(config->executor->primary, &caps);
        if (e != ESP_OK) {
            return e;
        }
        plan->preferred_executor = config->executor->primary;
        if (!executor_supports(&caps, backend_config)) {
            plan->executor_reason = PV2_EXEC_TRANSFORM;
            if (!config->executor->fallback) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            e = pv2_executor_get_caps(config->executor->fallback, &caps);
            if (e != ESP_OK) {
                return e;
            }
            if (!executor_supports(&caps, backend_config)) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            plan->preferred_executor = config->executor->fallback;
        }
        plan->address_probe_required = true;
        backend_config->copy_execute = pv2_executor_dispatch_execute;
        backend_config->copy_user = (void *)config->executor;
    }
    size_t alignment;
    if (!lcm(plan->output.base_alignment, PV2_BUFFER_ALIGNMENT, &alignment) ||
            !lcm(alignment, caps.base_alignment, &alignment) || !lcm(alignment, caps.destination_capacity_alignment, &alignment)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!power_two(alignment)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    plan->base_alignment = alignment;
    size_t stride_align;
    if (!lcm(plan->output.stride_alignment, caps.stride_alignment_bytes, &stride_align)) {
        return ESP_ERR_INVALID_SIZE;
    }
    bool quarter = backend_config->rotation == PV2_ROTATE_90 || backend_config->rotation == PV2_ROTATE_270;
    uint32_t pw = quarter ? backend_config->height : backend_config->width, ph = quarter ? backend_config->width : backend_config->height;
    if (!layout(backend_config->width, backend_config->height, bpp, caps.stride_alignment_bytes, backend_config->format, backend_config->draw_order, &plan->logical_layout) ||
            !layout(pw, ph, bpp, stride_align, backend_config->format, backend_config->panel_order, &plan->physical_layout)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (external_tx && images) {
        size_t transfer_bytes;
        if (!mul(plan->physical_layout.stride_bytes, ph, &transfer_bytes)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (transfer_bytes % length_alignment) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    if (plan->output.tight_stride && plan->physical_layout.stride_bytes != (size_t)pw * bpp) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (pw % plan->output.width_alignment || ph % plan->output.height_alignment) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((caps.max_stride_pixels && (plan->logical_layout.stride_bytes / bpp > caps.max_stride_pixels || plan->physical_layout.stride_bytes / bpp > caps.max_stride_pixels)) ||
            (caps.max_height && (backend_config->height > caps.max_height || ph > caps.max_height))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    size_t logical = 0;
    if (kind == PV2_PRESENTER_GRAM) {
        plan->topology = PV2_TOPOLOGY_REGION;
    } else if (partial) {
        plan->topology = PV2_TOPOLOGY_TILE_TO_PHYSICAL;
    } else if (backend_config->rotation != PV2_ROTATE_0 || backend_config->draw_order != backend_config->panel_order) {
        plan->topology = PV2_TOPOLOGY_LOGICAL_TO_PHYSICAL;
        logical = config->resources.logical_count ? config->resources.logical_count : 1;
    } else {
        plan->topology = PV2_TOPOLOGY_DIRECT_SURFACE;
    }
    if ((!logical && config->resources.logical_count) || logical > backend_config->max_frames) {
        return ESP_ERR_INVALID_ARG;
    }
    if (logical > 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (kind == PV2_PRESENTER_GRAM && backend_config->tile_rows < (quarter ? plan->output.width_alignment : plan->output.height_alignment)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    pv2_layout_t tile = plan->logical_layout;
    tile.height = partial ? backend_config->tile_rows : 0;
    if (!pool(&plan->tiles, tiles, tile, alignment, config->resources.tile_memory, false) ||
            !pool(&plan->images, images, plan->physical_layout, alignment, config->resources.image_memory, backend_config->external_buffers != NULL) ||
            !pool(&plan->logical, logical, plan->logical_layout, alignment, config->resources.logical_memory, false)) {
        return ESP_ERR_INVALID_SIZE;
    }
    bool convert = kind == PV2_PRESENTER_GRAM && (backend_config->async_copy || backend_config->rotation != PV2_ROTATE_0 || (backend_config->draw_order != backend_config->panel_order && !backend_config->in_place_convert));
    pv2_layout_t converted = tile;
    converted.byte_order = backend_config->panel_order;
    if (quarter) {
        if (!layout(tile.height, tile.width, bpp, stride_align, backend_config->format, backend_config->panel_order, &converted)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (!pool(&plan->conversion, convert ? tiles : 0, converted, alignment, backend_config->internal_tx_staging ? PV2_MEMORY_INTERNAL_DMA : config->resources.tile_memory, false)) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_plan_memory_t tx_memory = transport_memory;
    if ((tx_memory == PV2_MEMORY_INTERNAL_DMA && !plan->output.internal_dma) || (tx_memory == PV2_MEMORY_EXTERNAL && !plan->output.external_memory)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    plan->tx_capacity = config->resources.tx_capacity ? config->resources.tx_capacity : kind == PV2_PRESENTER_GRAM ? tiles : 1;
    if (!config->resources.tx_capacity && plan->output.max_inflight && plan->tx_capacity > plan->output.max_inflight) {
        plan->tx_capacity = plan->output.max_inflight;
    }
    if (!plan->tx_capacity || plan->tx_capacity > PV2_MAX_TILE_BUFFERS || (kind != PV2_PRESENTER_GRAM && plan->tx_capacity != 1) ||
            (plan->output.max_inflight && plan->tx_capacity > plan->output.max_inflight)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    plan->record_capacity = backend_config->max_frames;
    pv2_plan_pool_t *pools[] = {&plan->tiles, &plan->images, &plan->logical, &plan->conversion};
    for (size_t i = 0; i < 4; i++) {
        if (!add(plan->pixel_bytes, pools[i]->total_bytes, &plan->pixel_bytes)) {
            return ESP_ERR_INVALID_SIZE;
        }

    }
    size_t pixels;
    if (!mul(backend_config->width, backend_config->height, &pixels)) {
        return ESP_ERR_INVALID_SIZE;
    }
    plan->coverage_bytes = images ? pixels / 8 + (pixels % 8 != 0) : 0;
    if (config->pixel_budget_bytes && plan->pixel_bytes > config->pixel_budget_bytes) {
        return ESP_ERR_NO_MEM;
    }
    if (kind == PV2_PRESENTER_FB) {
        backend_config->tile_buffer_count = tiles;
        if (backend_config->external_buffers && images != backend_config->buffer_count) {
            return ESP_ERR_INVALID_ARG;
        }
        backend_config->buffer_count = images;
    } else {
        backend_config->buffer_count = tiles;
        if (kind == PV2_PRESENTER_TE) {
            backend_config->compose_count = images;
        }
    }
    if (backend_config->external_buffers) {
        bool found = backend_config->initial_scanout_buffer == NULL;
        if (backend_config->external_buffer_bytes < plan->images.bytes_each) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < images; i++) {
            uintptr_t base = (uintptr_t)backend_config->external_buffers[i];
            if (!base || base % alignment || base > UINTPTR_MAX - backend_config->external_buffer_bytes) {
                return ESP_ERR_INVALID_ARG;
            }
            if (backend_config->external_buffers[i] == backend_config->initial_scanout_buffer) {
                found = true;
            }
            for (size_t j = 0; j < i; j++) {
                uintptr_t other = (uintptr_t)backend_config->external_buffers[j];
                if (base < other + backend_config->external_buffer_bytes && other < base + backend_config->external_buffer_bytes) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
        if (!found) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (kind == PV2_PRESENTER_TE && !backend_config->te_gate.admit && backend_config->te_timing.period_us &&
            pv2_te_window(&backend_config->te_timing, backend_config->te_transfer_budget_us, 0, 0, false).admission == PV2_TE_UNSUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (kind == PV2_PRESENTER_TE && !backend_config->te_gate.admit && !backend_config->te_timing.period_us && (!backend_config->te_transfer_budget_us || backend_config->te_transfer_budget_us >= backend_config->te_window_us)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (plan->physical_layout.stride_bytes != (size_t)pw * bpp || plan->logical_layout.stride_bytes != (size_t)backend_config->width * bpp || alignment != PV2_BUFFER_ALIGNMENT) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    backend_config->tile_in_psram = config->resources.tile_memory == PV2_MEMORY_EXTERNAL;
    backend_config->image_in_psram = config->resources.image_memory == PV2_MEMORY_EXTERNAL;
    backend_config->logical_in_psram = config->resources.logical_memory == PV2_MEMORY_EXTERNAL;
    backend_config->transfer_capacity = plan->tx_capacity;
    plan->normalized.output_caps = plan->output;
    plan->metadata_bytes = pv2_presenter_memory(&plan->normalized) +
                           (kind == PV2_PRESENTER_GRAM ? pv2_gram_memory(backend_config) : kind == PV2_PRESENTER_TE ? pv2_te_memory(backend_config) : pv2_fb_memory(backend_config));
    plan->stack_bytes = backend_config->async_copy ? 4096 : 0;
    if (plan->normalized.background_service && !add(plan->stack_bytes,
                                                    plan->normalized.worker_stack_bytes ? plan->normalized.worker_stack_bytes : 4096, &plan->stack_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    plan->reserved_bytes = config->additional_budget_bytes;
    if (!add(plan->pixel_bytes, plan->coverage_bytes, &plan->required_bytes) ||
            !add(plan->required_bytes, plan->metadata_bytes, &plan->required_bytes) ||
            !add(plan->required_bytes, plan->stack_bytes, &plan->required_bytes) ||
            !add(plan->required_bytes, plan->reserved_bytes, &plan->required_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (config->total_budget_bytes && plan->required_bytes > config->total_budget_bytes) {
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
    pv2_resolved_plan_t plan = {0};
    esp_err_t e = resolve(config, &plan);
    if (e == ESP_OK) {
        *out = plan;
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
esp_err_t pv2_plan_render_area(const pv2_resolved_plan_t *plan, const pv2_rect_t *dirty, pv2_rect_t *render, pv2_rect_t *physical)
{
    if (!plan || !dirty || !render || !physical || !dirty->width || !dirty->height ||
            dirty->x > plan->logical_layout.width || dirty->y > plan->logical_layout.height ||
            dirty->width > plan->logical_layout.width - dirty->x || dirty->height > plan->logical_layout.height - dirty->y ||
            !plan->output.x_alignment || !plan->output.y_alignment || !plan->output.width_alignment || !plan->output.height_alignment) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t a = rotate(*dirty, plan->logical_layout.width, plan->logical_layout.height, plan->normalized.backend.rotation);
    if (plan->normalized.backend.render_contract == PV2_CONTRACT_FULL) {
        a = (pv2_rect_t) {
            0, 0, plan->physical_layout.width, plan->physical_layout.height
        };
    } else if (plan->images.count) {
        /* Full-frame output alignment is already satisfied by the candidate;
         * do not force a PARTIAL producer to repaint that entire image. */
        *physical = a;
        *render = *dirty;
        return ESP_OK;
    } else {
        size_t x = a.x - a.x % plan->output.x_alignment, y = a.y - a.y % plan->output.y_alignment;
        size_t w, h, wa, ha;
        if (!lcm(plan->output.x_alignment, plan->output.width_alignment, &wa) ||
                !lcm(plan->output.y_alignment, plan->output.height_alignment, &ha) ||
                !align_up((size_t)a.x + a.width - x, wa, &w) || !align_up((size_t)a.y + a.height - y, ha, &h)) {
            return ESP_ERR_INVALID_SIZE;
        }
        /* A shifted aligned origin can run past the edge even when the whole
         * axis is legal. Conservatively expand to that full axis, never clip. */
        if (w > plan->physical_layout.width - x) {
            x = 0;
            w = plan->physical_layout.width;
        }
        if (h > plan->physical_layout.height - y) {
            y = 0;
            h = plan->physical_layout.height;
        }
        if (w % wa || h % ha) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        a = (pv2_rect_t) {
            x, y, w, h
        };
    }
    *physical = a;
    pv2_rotation_t inverse = (4 - plan->normalized.backend.rotation) % 4;
    *render = rotate(a, plan->physical_layout.width, plan->physical_layout.height, inverse);
    return ESP_OK;
}
esp_err_t pv2_presenter_create_planned(const pv2_plan_config_t *config, pv2_presenter_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    pv2_resolved_plan_t plan;
    esp_err_t e = pv2_plan_resolve(config, &plan);
    if (e != ESP_OK) {
        return e;
    }
    if (plan.pending_runtime) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* DEFAULT prevents applying the named preset a second time. */
    plan.normalized.mode = PV2_MODE_DEFAULT;
    return pv2_presenter_create(&plan.normalized, out);
}
