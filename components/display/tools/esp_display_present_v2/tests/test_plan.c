/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_plan.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
static unsigned attachments;
static esp_err_t attach(void *ctx, pv2_panel_event_fn cb, void *user)
{
    (void)ctx;
    (void)cb;
    (void)user;
    attachments++;
    return ESP_OK;
}
static pv2_tx_result_t submit(void *ctx, const pv2_surface_t *s, const pv2_rect_t *r, uint64_t c)
{
    (void)ctx;
    (void)s;
    (void)r;
    (void)c;
    return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_FAIL
    };
}
static esp_err_t quiet(void *ctx, uint32_t t)
{
    (void)ctx;
    (void)t;
    return ESP_OK;
}
static pv2_plan_config_t config(pv2_presenter_backend_t kind)
{
    return (pv2_plan_config_t) {
        .presenter = {.backend_kind = kind, .backend = {.width = 480, .height = 480, .tile_rows = 16, .buffer_count = 2, .max_frames = 4,
                                                        .format = PV2_FORMAT_RGB565, .panel = {NULL, attach, submit, quiet}, .te_window_us = 1000, .te_transfer_budget_us = 100
                                                       }
                     },
        .output = {.internal_dma = true, .external_memory = true, .tight_stride = true}
    };
}
static void modes(void)
{
    struct {
        pv2_present_mode_t mode;
        size_t images;
        pv2_render_contract_t contract;
    } cases[] = {
        {PV2_MODE_NONE, 1, PV2_CONTRACT_PARTITION}, {PV2_MODE_DOUBLE_FULL, 2, PV2_CONTRACT_FULL},
        {PV2_MODE_TRIPLE_FULL, 3, PV2_CONTRACT_FULL}, {PV2_MODE_DOUBLE_DIRECT, 2, PV2_CONTRACT_DIRECT},
        {PV2_MODE_DOUBLE_PARTIAL, 2, PV2_CONTRACT_PARTITION}, {PV2_MODE_TRIPLE_PARTIAL, 3, PV2_CONTRACT_PARTITION}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pv2_plan_config_t c = config(PV2_PRESENTER_FB);
        c.presenter.mode = cases[i].mode;
        c.presenter.backend.allow_active_scanout_writes = true;
        pv2_resolved_plan_t p;
        assert(pv2_plan_resolve(&c, &p) == ESP_OK);
        assert(p.images.count == cases[i].images && p.normalized.backend.render_contract == cases[i].contract);
        assert(p.logical.count == 0);
        c.resources.image_count = cases[i].images == 2 ? 3 : 2;
        assert(pv2_plan_resolve(&c, &p) == ESP_ERR_INVALID_ARG);
    }
    pv2_resolved_plan_t p;
    pv2_plan_config_t c = config(PV2_PRESENTER_GRAM);
    c.presenter.mode = PV2_MODE_NONE;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && !p.images.count && p.tiles.count == 2);
    c.presenter.mode = PV2_MODE_AUTO;
    c.presenter.backend.te_window_us = 0;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.normalized.backend_kind == PV2_PRESENTER_GRAM);
    c.defaults = PV2_PLAN_V1_DEFAULTS;
    c.te_enabled = true;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
    c.presenter.backend.te_window_us = 1000;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.images.count == 1 && !p.pending_runtime);
    c = config(PV2_PRESENTER_FB);
    c.presenter.mode = PV2_MODE_AUTO;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.images.count == 2);
    c.defaults = PV2_PLAN_V1_DEFAULTS;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.images.count == 3);
    c.presenter.backend.rotation = PV2_ROTATE_90;
    c.presenter.mode = PV2_MODE_DOUBLE_DIRECT;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
}
static void topology_and_budget(void)
{
    pv2_plan_config_t c = config(PV2_PRESENTER_FB);
    c.presenter.backend.width = 1024;
    c.presenter.backend.height = 600;
    c.presenter.mode = PV2_MODE_TRIPLE_PARTIAL;
    c.presenter.backend.rotation = PV2_ROTATE_90;
    pv2_resolved_plan_t p;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK);
    assert(p.physical_layout.width == 600 && p.physical_layout.height == 1024);
    assert(p.topology == PV2_TOPOLOGY_TILE_TO_PHYSICAL && p.logical.count == 0);
    assert(p.images.total_bytes == 3686400 && p.tiles.total_bytes == 65536 && p.coverage_bytes == 76800);
    assert(!(p.pending_runtime & PV2_PLAN_NEEDS_ROTATION_TOPOLOGY));
    c.presenter.mode = PV2_MODE_TRIPLE_FULL;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.logical.count == 1 && !p.tiles.count);
    assert(p.pixel_bytes == 4915200 && p.logical.total_bytes == 1228800);
    c.pixel_budget_bytes = p.pixel_bytes - 1;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NO_MEM && p.pixel_bytes == 0);
    c.pixel_budget_bytes = 0;
    c.resources.logical_count = 2;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
    c.resources.logical_count = 1;
    /* FULL/DIRECT byte-order conversion uses the single logical canvas topology. */
    c.presenter.backend.rotation = PV2_ROTATE_0;
    c.presenter.backend.draw_order = PV2_ORDER_NATIVE;
    c.presenter.backend.panel_order = PV2_ORDER_SWAPPED;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.topology == PV2_TOPOLOGY_LOGICAL_TO_PHYSICAL && p.logical.count == 1 && !p.pending_runtime);
    c = config(PV2_PRESENTER_GRAM);
    c.presenter.backend.width = INT_MAX;
    c.presenter.backend.height = INT_MAX;
    c.presenter.backend.tile_rows = INT_MAX;
    c.resources.tile_count = 16;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_INVALID_SIZE);
    c = config(PV2_PRESENTER_TE);
    c.resources.image_memory = PV2_MEMORY_EXTERNAL;
    c.output.external_memory = false;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
}
/* Independent per-pixel mapping checks rectangle coverage in all rotations. */
static void map(unsigned x, unsigned y, unsigned w, unsigned h, unsigned rot, unsigned *px, unsigned *py)
{
    if (rot == 0) {
        *px = x;
        *py = y;
    } else if (rot == 1) {
        *px = h - 1 - y;
        *py = x;
    } else if (rot == 2) {
        *px = w - 1 - x;
        *py = h - 1 - y;
    } else {
        *px = y;
        *py = w - 1 - x;
    }
}
static void alignment(void)
{
    for (unsigned rot = 0; rot < 4; rot++) {
        pv2_plan_config_t c = config(PV2_PRESENTER_GRAM);
        c.presenter.backend.width = 12;
        c.presenter.backend.height = 8;
        c.presenter.backend.tile_rows = 4;
        c.presenter.backend.rotation = rot;
        c.output.x_alignment = 2;
        c.output.width_alignment = 4;
        c.output.y_alignment = 2;
        c.output.height_alignment = 2;
        pv2_resolved_plan_t p;
        assert(pv2_plan_resolve(&c, &p) == ESP_OK);
        pv2_rect_t dirty = {3, 1, 3, 3}, r, a;
        assert(pv2_plan_render_area(&p, &dirty, &r, &a) == ESP_OK);
        assert(a.x % 2 == 0 && a.width % 4 == 0 && a.y % 2 == 0 && a.height % 2 == 0);
        pv2_rect_t edge = {11, 7, 1, 1}, er, ep;
        assert(pv2_plan_render_area(&p, &edge, &er, &ep) == ESP_OK);
        assert(ep.x + ep.width <= p.physical_layout.width && ep.y + ep.height <= p.physical_layout.height);
        for (unsigned y = dirty.y; y < dirty.y + dirty.height; y++)
            for (unsigned x = dirty.x; x < dirty.x + dirty.width; x++) {
                unsigned px, py;
                map(x, y, 12, 8, rot, &px, &py);
                assert(px >= a.x && px < a.x + a.width && py >= a.y && py < a.y + a.height);
                assert(x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height);
            }
    }
    pv2_plan_config_t c = config(PV2_PRESENTER_GRAM);
    pv2_resolved_plan_t p;
    c.presenter.backend.width = 7;
    c.output.width_alignment = 4;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
    c = config(PV2_PRESENTER_GRAM);
    c.presenter.backend.format = PV2_FORMAT_RGB888;
    c.presenter.backend.width = 5;
    c.output.stride_alignment = 64;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED); /* tight row cannot be padded */
    c.output.tight_stride = false;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED); /* non-tight padded pool layout rejected */
}
static void creation(void)
{
    pv2_plan_config_t partial = config(PV2_PRESENTER_FB);
    partial.output.full_frame_only = true;
    pv2_resolved_plan_t plan;
    assert(pv2_plan_resolve(&partial, &plan) == ESP_OK);
    pv2_rect_t dirty = {3, 4, 5, 6}, render, physical;
    assert(pv2_plan_render_area(&plan, &dirty, &render, &physical) == ESP_OK);
    assert(!memcmp(&render, &dirty, sizeof(dirty)));
    pv2_plan_config_t c = config(PV2_PRESENTER_FB);
    c.presenter.mode = PV2_MODE_DOUBLE_PARTIAL;
    pv2_presenter_t *presenter = NULL;
    unsigned before = attachments;
    assert(pv2_presenter_create_planned(&c, &presenter) == ESP_OK && attachments == before + 1);
    assert(pv2_presenter_stop(presenter, 0) == ESP_OK);
    assert(pv2_presenter_destroy(presenter) == ESP_OK);
    before = attachments;
    c.resources.tile_count = 1;
    assert(pv2_presenter_create_planned(&c, &presenter) == ESP_OK && attachments == before + 1);
    assert(pv2_presenter_stop(presenter, 0) == ESP_OK);
    assert(pv2_presenter_destroy(presenter) == ESP_OK);
    c = config(PV2_PRESENTER_TE);
    before = attachments;
    assert(pv2_presenter_create_planned(&c, &presenter) == ESP_OK && attachments == before + 1);
    assert(pv2_presenter_stop(presenter, 0) == ESP_OK);
    assert(pv2_presenter_destroy(presenter) == ESP_OK);
    c = config(PV2_PRESENTER_GRAM);
    assert(pv2_presenter_create_planned(&c, &presenter) == ESP_OK);
    assert(pv2_presenter_stop(presenter, 0) == ESP_OK);
    assert(pv2_presenter_destroy(presenter) == ESP_OK);
}
static void reverse_band_alignment(void)
{
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        pv2_plan_config_t c = config(PV2_PRESENTER_GRAM);
        c.presenter.backend.rotation = rotation;
        c.output.x_alignment = c.output.y_alignment = 4;
        c.output.width_alignment = c.output.height_alignment = 2;
        pv2_resolved_plan_t p;
        assert(pv2_plan_resolve(&c, &p) == ESP_OK);
        pv2_rect_t dirty = {3, 3, 6, 18}, render, physical;
        assert(pv2_plan_render_area(&p, &dirty, &render, &physical) == ESP_OK);
        assert(physical.width % 4 == 0 && physical.height % 4 == 0);
        /* Every forward/reversed four-row band retains a legal origin. */
        assert((physical.x + physical.width - 4) % 4 == 0);
        assert((physical.y + physical.height - 4) % 4 == 0);
    }
}
static void external_dma_alignment(void)
{
    pv2_plan_config_t c = config(PV2_PRESENTER_GRAM);
    c.resources.tile_memory = PV2_MEMORY_EXTERNAL;
    c.output.external_memory = true;
    c.output.external_length_alignment = 64;
    pv2_resolved_plan_t p;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK);
    assert(p.output.width_alignment == 32);
    pv2_rect_t dirty = {1, 1, 1, 1}, render, physical;
    assert(pv2_plan_render_area(&p, &dirty, &render, &physical) == ESP_OK);
    assert((size_t)physical.width * physical.height * 2 % 64 == 0);
    c = config(PV2_PRESENTER_TE);
    c.resources.image_memory = PV2_MEMORY_EXTERNAL;
    c.output.external_memory = true;
    c.output.external_length_alignment = 64;
    c.presenter.backend.width = 31;
    c.presenter.backend.height = 31;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
}
static void timing_model(void)
{
    pv2_plan_config_t c = config(PV2_PRESENTER_TE);
    c.presenter.backend.te_timing = (pv2_te_timing_t) {
        .period_us = 100, .scan_us = 101
    };
    c.presenter.backend.te_transfer_budget_us = 50;
    pv2_resolved_plan_t p;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
    c.presenter.backend.te_timing.scan_us = 80;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK);
}
static void total_budget(void)
{
    pv2_plan_config_t c = config(PV2_PRESENTER_FB);
    c.additional_budget_bytes = 12345;
    pv2_resolved_plan_t p;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK);
    assert(p.metadata_bytes && p.required_bytes == p.pixel_bytes + p.coverage_bytes + p.metadata_bytes + p.stack_bytes + 12345);
    c.total_budget_bytes = p.required_bytes - 1;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NO_MEM);
}
static esp_err_t fake_caps(void *ctx, pv2_executor_caps_t *c)
{
    (void)ctx;
    *c = (pv2_executor_caps_t) {
        .format_mask = 3, .rotation_mask = 1, .base_alignment = 64, .stride_alignment_bytes = 64
    };
    return ESP_OK;
}
static esp_err_t forbidden_probe(void *ctx, const pv2_copy_request_t *r, pv2_executor_reason_t *reason)
{
    (void)ctx;
    (void)r;
    (void)reason;
    assert(0);
    return ESP_FAIL;
}
static esp_err_t forbidden_execute(void *ctx, const pv2_copy_request_t *r)
{
    (void)ctx;
    (void)r;
    assert(0);
    return ESP_FAIL;
}
static esp_err_t fake_destroy(void *ctx)
{
    (void)ctx;
    return ESP_OK;
}
static void executor(void)
{
    const pv2_executor_ops_t ops = {fake_caps, forbidden_probe, forbidden_execute, fake_destroy};
    pv2_executor_t fake = {&ops, NULL}, *cpu;
    assert(pv2_executor_cpu_create(&cpu) == ESP_OK);
    pv2_executor_dispatch_t d = {&fake, cpu};
    pv2_plan_config_t c = config(PV2_PRESENTER_TE);
    c.executor = &d;
    c.presenter.backend.async_copy = true;
    c.presenter.backend.rotation = PV2_ROTATE_90;
    pv2_resolved_plan_t p;
    assert(pv2_plan_resolve(&c, &p) == ESP_OK && p.preferred_executor == cpu && p.address_probe_required);
    d.fallback = NULL;
    assert(pv2_plan_resolve(&c, &p) == ESP_ERR_NOT_SUPPORTED);
    assert(pv2_executor_destroy(cpu) == ESP_OK);
}
int main(void)
{
    reverse_band_alignment();
    external_dma_alignment();
    timing_model();
    total_budget();
    modes();
    topology_and_budget();
    alignment();
    creation();
    executor();
    puts("plan: mode compatibility, topology, budgets, alignment, pure capabilities and fail-before-attach passed");
    return 0;
}
