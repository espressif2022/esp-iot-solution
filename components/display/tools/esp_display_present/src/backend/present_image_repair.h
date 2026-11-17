/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_records.h"
#include "platform/present_allocation.h"
/**
 * @file
 * @brief Output-independent image coverage and repair bookkeeping (owner-task serialized).
 */
/** @brief Image repair history and slot capacity constants. */
enum {
    PRESENT_IMAGE_SLOTS = 3,   /**< Maximum tracked image slots. */
    PRESENT_IMAGE_HISTORY = 8, /**< Ring depth for recent frame damage deltas. */
};

/** @brief Bounded damage rectangle set or full-frame flag. */
typedef struct {
    esp_display_present_rect_t areas[PRESENT_MAX_DAMAGE_AREAS]; /**< Tracked damage rectangles. */
    size_t count;                                               /**< Number of valid entries in @c areas. */
    bool full;                                                  /**< True when full-frame coverage applies. */
} present_damage_set_t;

/** @brief Versioned frame delta entry recorded at image commit. */
typedef struct {
    uint64_t version;           /**< Published image version. */
    present_damage_set_t damage;/**< Covered damage associated with @c version. */
} present_image_delta_t;

/** @brief Multi-slot image coverage, repair cursor, and allocation reference state. */
typedef struct {
    uint32_t width;  /**< Surface width in pixels. */
    uint32_t height; /**< Surface height in pixels. */
    present_allocation_t allocations[PRESENT_IMAGE_SLOTS]; /**< Per-slot writer/reader reference tracking. */
    bool allow_scanout_writes; /**< Permit writes while scanning out (single-FB tearing mode). */
    bool baseline_borrow;      /**< Retain baseline references during active frame build. */
    size_t published_baseline; /**< Currently published baseline slot index. */
    uint8_t *coverage;         /**< Lazy pixel coverage bitmap (NULL while rectangle sets suffice). */
    size_t coverage_bytes;     /**< Byte size of @c coverage bitmap. */
    uint64_t versions[PRESENT_IMAGE_SLOTS]; /**< Per-slot published content versions. */
    uint64_t latest;           /**< Latest published monotonic content version. */
    present_image_delta_t history[PRESENT_IMAGE_HISTORY]; /**< Recent published frame deltas. */
    present_damage_set_t writes; /**< Producer-written areas in active candidate. */
    present_damage_set_t repair; /**< Pending repair spans to copy from baseline. */
    present_damage_set_t exact;  /**< Exact rectangle coverage accumulated before bitmap fallback. */
    present_damage_set_t seeded; /**< Pre-seeded baseline rectangles (excluded from app coverage). */
    esp_err_t coverage_error;    /**< Sticky coverage allocation error. */
    size_t candidate;            /**< Active candidate slot index. */
    size_t baseline;             /**< Active baseline slot index. */
    size_t rect;                 /**< Current repair rectangle index. */
    size_t offset;               /**< Pixel offset cursor within current repair rectangle. */
    bool repair_ready;           /**< True once repair spans have been prepared. */
    bool full_covered;           /**< True when candidate is fully covered. */
    bool bitmap_dirty;           /**< True when coverage bitmap needs clearing. */
    size_t repaired_pixels;      /**< Count of pixels repaired for telemetry/testing. */
} present_image_builder_t;
/** @brief Initialize image coverage/repair bookkeeping for @p width x @p height. */
esp_err_t present_image_init(present_image_builder_t *, uint32_t, uint32_t);
/** @brief Release image bookkeeping resources. */
void present_image_deinit(present_image_builder_t *);
/** @brief Reset image bookkeeping and baselines for new dimensions. */
void present_image_reset(present_image_builder_t *, uint32_t, uint32_t);
/** @brief Start building @p candidate relative to @p baseline. */
esp_err_t present_image_begin(present_image_builder_t *, size_t candidate, size_t baseline);
/** @brief Record application-written coverage rectangle. */
esp_err_t present_image_cover(present_image_builder_t *, esp_display_present_rect_t);
/** @brief Record baseline-seeded pixels without counting them as application coverage. */
esp_err_t present_image_seed(present_image_builder_t *, esp_display_present_rect_t);
/** @brief Return true if the candidate has full effective coverage. */
bool present_image_full(const present_image_builder_t *);
/** @brief Compute missing repair spans relative to the active baseline. */
void present_image_prepare_repair(present_image_builder_t *, bool defensive_direct);
/** @brief Peek the next pending repair span without marking it repaired yet. */
bool present_image_next_repair(present_image_builder_t *, esp_display_present_rect_t *);
/** @brief Advance repair state after synchronous copy or accepted async submission of @p span. */
esp_err_t present_image_mark_repaired(present_image_builder_t *, const esp_display_present_rect_t *);
/** @brief Commit completed candidate coverage and publish new image history version. */
esp_err_t present_image_commit(present_image_builder_t *);
/** @brief Abort active candidate build without publishing history. */
void present_image_cancel(present_image_builder_t *);
