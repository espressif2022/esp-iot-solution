/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "platform/present_os.h"
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
/**
 * @brief Pixel-buffer borrow reference kinds preventing concurrent writes.
 *
 * Independent of frame receipts and content versions; confirming an FB frame
 * does not release its active scanout reference.
 */
typedef enum {
    PRESENT_REF_BASELINE, /**< Retained as content baseline for future incremental repair. */
    PRESENT_REF_COPY,     /**< Read by copy/repair executor. */
    PRESENT_REF_TRANSFER, /**< Borrowed by panel DMA / transfer submission. */
    PRESENT_REF_SCANOUT,  /**< Actively scanning out on display panel. */
    PRESENT_REF_COUNT,    /**< Number of reference kinds. */
} present_ref_kind_t;

/** @brief Per-buffer writer and reader-reference counters (caller-serialized). */
typedef struct {
    size_t readers[PRESENT_REF_COUNT]; /**< Active reader counts per reference kind. */
    bool writer;                       /**< True while leased to a writer. */
} present_allocation_t;

/** @brief Acquire write access if no conflicting copy/transfer/baseline/scanout reference exists. */
static inline bool present_allocation_write(present_allocation_t *allocation, bool retained, bool tearing)
{
    if (allocation->writer || allocation->readers[PRESENT_REF_COPY] || allocation->readers[PRESENT_REF_TRANSFER] ||
            (!retained && allocation->readers[PRESENT_REF_BASELINE]) || (!tearing && allocation->readers[PRESENT_REF_SCANOUT])) {
        return false;
    }
    allocation->writer = true;
    return true;
}
/** @brief Assert and claim exclusive write access on an unreferenced allocation. */
static inline void present_allocation_claim(present_allocation_t *allocation)
{
    bool claimed = present_allocation_write(allocation, false, false);
    assert(claimed);
    (void)claimed;
}
/** @brief Release active write access after producer submission/cancellation. */
static inline void present_allocation_written(present_allocation_t *allocation)
{
    assert(allocation->writer);
    allocation->writer = false;
}
/** @brief Increment a reader reference of @p kind. */
static inline void present_allocation_hold(present_allocation_t *allocation, present_ref_kind_t kind)
{
    assert(!allocation->writer && allocation->readers[kind] != (size_t) -1);
    allocation->readers[kind]++;
}
/** @brief Decrement a reader reference of @p kind (ISR-safe). */
static inline void PRESENT_ISR_ATTR present_allocation_drop(present_allocation_t *allocation, present_ref_kind_t kind)
{
    assert(allocation->readers[kind]);
    allocation->readers[kind]--;
}
