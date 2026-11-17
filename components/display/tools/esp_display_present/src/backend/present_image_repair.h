/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_records.h"
#include "platform/present_allocation.h"
/* Output-independent image construction bookkeeping. Owner task serialized.
 * Coverage is exact; history may conservatively widen, never invent coverage.
 * No hardware, frame receipts, TE phases or scanout state in this module. */
enum {
    PRESENT_IMAGE_SLOTS = 3,
    PRESENT_IMAGE_HISTORY = 8,
};
typedef struct {
    esp_display_present_rect_t areas[PRESENT_MAX_DAMAGE_AREAS];
    size_t count;
    bool full;
} present_damage_set_t;
typedef struct {
    uint64_t version;
    present_damage_set_t damage;
} present_image_delta_t;
typedef struct {
    uint32_t width, height;
    present_allocation_t allocations[PRESENT_IMAGE_SLOTS];
    bool allow_scanout_writes, baseline_borrow;
    size_t published_baseline;
    uint8_t *coverage;
    size_t coverage_bytes;
    uint64_t versions[PRESENT_IMAGE_SLOTS], latest;
    present_image_delta_t history[PRESENT_IMAGE_HISTORY];
    present_damage_set_t writes, repair, exact, seeded;
    esp_err_t coverage_error;
    size_t candidate, baseline, rect, offset;
    bool repair_ready, full_covered, bitmap_dirty;
    size_t repaired_pixels;
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
