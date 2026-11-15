/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_draw_types.h"
#include "esp_err.h"
#include "esp_display_present_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t value;
} esp_display_present_lease_id_t;

typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL,
    ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT,
} esp_display_present_render_contract_t;

typedef struct {
    esp_display_present_render_contract_t contract;
    esp_display_present_layout_t draw_layout;
    size_t drawbuf_capacity_bytes; /* Per-slot writable capacity in bytes. */
    size_t buffer_alignment;       /* Power-of-two base alignment in bytes. */
    uint8_t draw_buffer_count;
    uint8_t tile_count;
    uint8_t image_count;
    esp_display_present_memory_domain_t draw_memory;
    esp_display_present_memory_domain_t image_memory;
    uint32_t max_tile_rows;
    uint32_t max_damage_areas;
    bool retains_content;
    bool supports_present_confirmation;
    bool writes_active_scanout; /* Explicit persistent single-FB contract. */
} esp_display_present_backend_caps_t;

typedef struct {
    const esp_display_present_rect_t *damage;
    size_t damage_count;
    /* Producer redraws every pixel without relying on the old contents of
     * the acquired surface. Actual coverage is still checked before publish
     * if prior pixels were not seeded. */
    bool full_redraw;
    /* DIRECT only: submitted regions contain resolved pixels from an
     * independent producer canvas. The producer never reads old lease pixels.
     * Skip readable prefill; repair missing image history after actual writes.
     * This does not promise full coverage or relax framebuffer retirement. */
    bool resolved_pixels;
} esp_display_present_frame_request_t;

typedef struct {
    esp_display_present_lease_id_t id;
    esp_display_present_surface_t surface;
    esp_display_present_rect_t logical_area; /* Surface origin maps to logical_area origin. */
} esp_display_present_lease_t;

/** Optional explicit dirty coverage when sealing a FULL/DIRECT frame.
 * Ignored by region (which tracks each submitted partition region directly). */
typedef struct {
    const esp_display_present_rect_t *areas;
    size_t area_count;
    bool full_coverage;
} esp_display_present_frame_coverage_t;

typedef struct esp_display_present_engine esp_display_present_engine_t;
typedef struct {
    int32_t x1, y1, x2, y2;
} esp_display_present_engine_area_t; /* Inclusive, v1-style. */
typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_RUNNING,
    ESP_DISPLAY_PRESENT_ENGINE_CLOSING,
    ESP_DISPLAY_PRESENT_ENGINE_STOPPED,
} esp_display_present_engine_state_t;
typedef struct {
    esp_display_present_engine_state_t state;
    bool frame_active;
    bool cancel_pending;
    size_t pending_frames;
    uint64_t committed_frames;
    uint64_t completed_frames; /* Terminal transfers, not necessarily successful. */
    esp_err_t first_error; /* Sticky accepted-operation error, not a transient rejection. */
    esp_err_t last_service_error; /* Latest pump result, including retryable rejection. */
} esp_display_present_engine_status_t;

#ifdef __cplusplus
}
#endif
