/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_records.h"
#include "present_allocation.h"
/* Output-independent image construction bookkeeping. Owner task serialized.
 * Coverage is exact; history may conservatively widen, never invent coverage.
 * No hardware, frame receipts, TE phases or scanout state in this module. */
enum { PV2_IMAGE_SLOTS = 3, PV2_IMAGE_HISTORY = 8 };
typedef struct {
    pv2_rect_t areas[PV2_MAX_DAMAGE_AREAS];
    size_t count;
    bool full;
} pv2_damage_set_t;
typedef struct {
    uint64_t version;
    pv2_damage_set_t damage;
} pv2_image_delta_t;
typedef struct {
    uint32_t width, height;
    pv2_allocation_t allocations[PV2_IMAGE_SLOTS];
    bool allow_scanout_writes, baseline_borrow;
    size_t published_baseline;
    uint8_t *coverage;
    size_t coverage_bytes;
    uint64_t versions[PV2_IMAGE_SLOTS], latest;
    pv2_image_delta_t history[PV2_IMAGE_HISTORY];
    pv2_damage_set_t writes, repair;
    size_t candidate, baseline, rect, offset;
    bool repair_ready;
    size_t repaired_pixels;
} pv2_image_builder_t;
esp_err_t pv2_image_init(pv2_image_builder_t *, uint32_t, uint32_t);
void pv2_image_deinit(pv2_image_builder_t *);
void pv2_image_reset(pv2_image_builder_t *, uint32_t, uint32_t);
esp_err_t pv2_image_begin(pv2_image_builder_t *, size_t candidate, size_t baseline);
void pv2_image_cover(pv2_image_builder_t *, pv2_rect_t);
bool pv2_image_full(const pv2_image_builder_t *);
void pv2_image_prepare_repair(pv2_image_builder_t *, bool defensive_direct);
bool pv2_image_next_repair(pv2_image_builder_t *, pv2_rect_t *);
void pv2_image_repaired(pv2_image_builder_t *, const pv2_rect_t *);
esp_err_t pv2_image_commit(pv2_image_builder_t *);
void pv2_image_cancel(pv2_image_builder_t *);
