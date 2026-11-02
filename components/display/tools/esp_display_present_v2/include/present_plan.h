/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_presenter.h"
#include "present_executor.h"
#include "present_output_caps.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Select default pool/mode policy, not a ceiling on supported capabilities. */
typedef enum { PV2_PLAN_V2_DEFAULTS, PV2_PLAN_V1_DEFAULTS } pv2_plan_defaults_t;
typedef enum { PV2_MEMORY_INTERNAL_DMA, PV2_MEMORY_EXTERNAL } pv2_plan_memory_t;
typedef enum {
    PV2_TOPOLOGY_REGION,
    PV2_TOPOLOGY_TILE_TO_PHYSICAL,
    PV2_TOPOLOGY_DIRECT_SURFACE,
    PV2_TOPOLOGY_LOGICAL_TO_PHYSICAL,
} pv2_plan_topology_t;
/* Independent pool quantities; zero selects defaults. Explicit overrides must
 * agree with named mode image counts. Memory kind is explicit; no implicit staging allocation. */
typedef struct {
    size_t tile_count, image_count, logical_count, tx_capacity;
    pv2_plan_memory_t tile_memory, image_memory, logical_memory;
} pv2_plan_resources_t;
typedef struct {
    pv2_presenter_config_t presenter;
    pv2_plan_defaults_t defaults;
    bool te_enabled; /* V1 AUTO only; does not invent TE timing measurements. */
    pv2_plan_resources_t resources;
    pv2_output_caps_t output;
    /* Borrowed dispatcher and executors must outlive the created presenter.
     * Resolve queries caps only; actual address probe remains mandatory. */
    const pv2_executor_dispatch_t *executor;
    size_t additional_budget_bytes; /* Driver, renderer and allocator overhead reservation. */
    size_t total_budget_bytes; /* 0 unlimited; framework requests + caller reservation. */
    size_t pixel_budget_bytes; /* 0 unlimited; includes borrowed pixel buffers. */
} pv2_plan_config_t;
typedef struct {
    size_t count, bytes_each, total_bytes;
    pv2_layout_t layout;
    pv2_plan_memory_t memory;
    bool borrowed;
} pv2_plan_pool_t;
/* Bits indicate missing runtime paths, not malformed configurations. */
enum {
    PV2_PLAN_NEEDS_FB_TILE_POOL = 1u << 0,
    PV2_PLAN_NEEDS_ROTATION_TOPOLOGY = 1u << 1,
    PV2_PLAN_NEEDS_TE_POOL = 1u << 2,
    PV2_PLAN_NEEDS_MEMORY_PATH = 1u << 3,
    PV2_PLAN_NEEDS_PADDED_LAYOUT = 1u << 4,
    PV2_PLAN_NEEDS_REGION_CONTRACT = 1u << 5,
    PV2_PLAN_NEEDS_DYNAMIC_TE = 1u << 6,
    PV2_PLAN_NEEDS_TE_PIPELINE = 1u << 7,
    PV2_PLAN_NEEDS_TX_LIMIT = 1u << 8,
};
typedef struct {
    pv2_presenter_config_t normalized;
    pv2_plan_topology_t topology;
    pv2_layout_t logical_layout, physical_layout;
    pv2_plan_pool_t tiles, images, logical, conversion;
    pv2_output_caps_t output;
    size_t tx_capacity, record_capacity, base_alignment;
    size_t pixel_bytes, coverage_bytes; /* coverage_bytes reserves worst-case lazy bitmap, not resident allocation. */
    size_t metadata_bytes, stack_bytes, reserved_bytes, required_bytes;
    /* required_bytes includes pixel + coverage + framework metadata + requested
     * task stacks + caller reservation. Add driver/renderer/allocator overhead
     * through additional_budget_bytes. Host pthread stack size is OS-defined. */
    const pv2_executor_t *preferred_executor;
    pv2_executor_reason_t executor_reason;
    bool address_probe_required;
    uint32_t pending_runtime;
} pv2_resolved_plan_t;

/* Pure planning: no allocation/attach/driver calls; get_caps only. Invalid or
 * unsupported physical constraints return errors; valid future topologies are
 * described with pending_runtime bits and cannot be created by today's bridge. */
esp_err_t pv2_plan_resolve(const pv2_plan_config_t *config, pv2_resolved_plan_t *plan);
/* Dirty L -> aligned P -> required render L, before rasterization. No clipping
 * of an expanded physical edge that would violate alignment. No pixel reads. */
esp_err_t pv2_plan_render_area(const pv2_resolved_plan_t *plan, const pv2_rect_t *dirty,
                               pv2_rect_t *render, pv2_rect_t *physical);
/* Re-resolves rather than trusting a caller-mutated plan. Pending runtime paths
 * reject BEFORE any allocation or callback attachment; no legacy fallback. */
esp_err_t pv2_presenter_create_planned(const pv2_plan_config_t *config, pv2_presenter_t **out);
#ifdef __cplusplus
}
#endif
