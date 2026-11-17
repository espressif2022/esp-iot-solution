/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "config/present_engine_config.h"
#include "core/present_engine_types.h"
#include "config/present_mode.h"
#include "pixel/present_executor.h"
#include "backend/present_output_caps.h"
#ifdef __cplusplus
extern "C" {
#endif

/** @brief Memory domain preference for a planned buffer pool. */
typedef enum {
    ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA, /**< Internal DMA-capable RAM. */
    ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL,     /**< External PSRAM. */
} esp_display_present_plan_memory_t;

/** @brief Resolved buffer-dataflow topology. */
typedef enum {
    ESP_DISPLAY_PRESENT_TOPOLOGY_REGION,             /**< Partial tiles directly to panel-IO (with optional conversion). */
    ESP_DISPLAY_PRESENT_TOPOLOGY_TILE_TO_PHYSICAL,   /**< Partial tiles composed/copied into physical target frames. */
    ESP_DISPLAY_PRESENT_TOPOLOGY_DIRECT_SURFACE,     /**< Direct rendering into physical framebuffers. */
    ESP_DISPLAY_PRESENT_TOPOLOGY_LOGICAL_TO_PHYSICAL,/**< Full logical staging canvas converted to physical framebuffers. */
} esp_display_present_plan_topology_t;

/**
 * @brief Planned pool counts and memory domains (zero selects defaults).
 *
 * @c tiles are presenter input buffers (distinct from TE compose images or FBs).
 * Explicit overrides must agree with named mode counts.
 */
typedef struct {
    size_t tile_count;    /**< Partial input tile count (0 selects default). */
    size_t image_count;   /**< Physical FB or TE compose image count (0 selects default). */
    size_t logical_count; /**< Logical full-frame staging canvas count (0 selects default). */
    size_t tx_capacity;   /**< Maximum in-flight panel transfers (0 selects default). */
    esp_display_present_plan_memory_t tile_memory;    /**< Memory preference for tile pool. */
    esp_display_present_plan_memory_t image_memory;   /**< Memory preference for image/FB pool. */
    esp_display_present_plan_memory_t logical_memory; /**< Memory preference for logical staging canvas. */
} esp_display_present_plan_resources_t;

/** @brief Input configuration for planning a presentation engine. */
typedef struct {
    esp_display_present_engine_config_t presenter;  /**< Engine and backend base configuration. */
    esp_display_present_plan_defaults_t defaults;   /**< Compatibility default policy. */
    bool te_enabled;                                /**< Legacy AUTO hint; does not invent TE timing. */
    esp_display_present_plan_resources_t resources; /**< Pool counts and memory domains. */
    esp_display_present_output_caps_t output;       /**< Target output alignments and memory capabilities. */
    const esp_display_present_executor_dispatch_t *executor; /**< Optional borrowed hardware/fallback executor dispatch. */
    size_t extra_bytes;                             /**< Caller reservation for driver/renderer/allocator overhead. */
    size_t total_budget_bytes;                      /**< Total budget ceiling in bytes (0 unlimited). */
    size_t pixel_budget_bytes;                      /**< Pixel pool budget ceiling in bytes (0 unlimited, includes borrowed buffers). */
} esp_display_present_plan_config_t;

/** @brief Planned capacity and memory domain for a single buffer pool. */
typedef struct {
    size_t count;       /**< Number of buffers in this pool. */
    size_t bytes_each;  /**< Aligned capacity of each buffer in bytes. */
    size_t total_bytes; /**< Total planned bytes across all buffers in this pool. */
    esp_display_present_layout_t layout;       /**< Buffer surface layout. */
    esp_display_present_plan_memory_t memory;  /**< Memory domain selected for this pool. */
    bool borrowed;                             /**< True when buffers are driver/caller-owned. */
} esp_display_present_plan_pool_t;

/**
 * @brief Resolved plan with normalized engine config, pools, and peak memory budgets.
 *
 * Distinguishes peak resource budget (@c pixel_bytes / @c required_bytes),
 * pool planned capacity, and actual runtime allocation; passing resolution
 * reserves no heap memory.
 */
typedef struct {
    esp_display_present_engine_config_t normalized; /**< Normalized engine configuration ready for creation. */
    esp_display_present_plan_topology_t topology;   /**< Selected buffer-flow topology. */
    esp_display_present_layout_t logical_layout;    /**< Logical producer layout. */
    esp_display_present_layout_t physical_layout;   /**< Physical panel layout. */
    esp_display_present_plan_pool_t tiles;          /**< Planned partial input tile pool. */
    esp_display_present_plan_pool_t images;         /**< Planned FB or TE compose pool. */
    esp_display_present_plan_pool_t logical;        /**< Planned logical staging canvas pool. */
    esp_display_present_plan_pool_t conversion;     /**< Planned REGION conversion/staging pool. */
    esp_display_present_output_caps_t output;       /**< Resolved output capabilities and alignments. */
    size_t tx_capacity;                             /**< Resolved max in-flight transfers. */
    size_t record_capacity;                         /**< Frame record capacity. */
    size_t base_alignment;                          /**< Required power-of-two buffer base alignment in bytes. */
    size_t pixel_bytes;                             /**< Peak pixel pool budget across all planned pools. */
    size_t coverage_bytes;                          /**< Worst-case lazy coverage bitmap budget in bytes. */
    size_t metadata_bytes;                          /**< Framework metadata allocation estimate in bytes. */
    size_t stack_bytes;                             /**< Peak worker task stack budget in bytes. */
    size_t reserved_bytes;                          /**< Caller @c extra_bytes reservation (not heap-allocated). */
    size_t required_bytes;                          /**< Total peak budget: pixel + coverage + metadata + stack + reserved. */
    const esp_display_present_executor_t *preferred_executor; /**< Executor selected during capability check. */
    esp_display_present_executor_reason_t executor_reason;    /**< Primary executor selection or fallback reason. */
    bool probe_addrs;                               /**< True when runtime address probing is required. */
} esp_display_present_resolved_plan_t;

/** @brief Resolve and validate a plan configuration without allocating memory or calling drivers. */
esp_err_t esp_display_present_plan_resolve(const esp_display_present_plan_config_t *config, esp_display_present_resolved_plan_t *plan);

/** @brief Map dirty logical area @p dirty to aligned @p physical and expanded @p render logical area without clipping. */
esp_err_t esp_display_present_plan_render_area(const esp_display_present_resolved_plan_t *plan, const esp_display_present_rect_t *dirty,
                                               esp_display_present_rect_t *render, esp_display_present_rect_t *physical);

/** @brief Re-resolve @p config and create a presentation engine before any allocation or attachment. */
esp_err_t esp_display_present_engine_create_planned(const esp_display_present_plan_config_t *config, esp_display_present_engine_t **out);

/** @brief Internal single-resolve creation entry returning the resolved @p out_plan used for creation. */
esp_err_t present_engine_create_with_plan(const esp_display_present_plan_config_t *config,
                                          esp_display_present_resolved_plan_t *out_plan,
                                          esp_display_present_engine_t **out);
#ifdef __cplusplus
}
#endif
