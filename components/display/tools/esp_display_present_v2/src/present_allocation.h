/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_os.h"
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
/* Caller serialized. References are independent of frame receipts and content
 * versions. A confirmed FB receipt may still retain S indefinitely. */
typedef enum { PV2_REF_BASELINE, PV2_REF_COPY, PV2_REF_TRANSFER, PV2_REF_SCANOUT, PV2_REF_COUNT } pv2_ref_kind_t;
typedef struct {
    size_t readers[PV2_REF_COUNT];
    bool writer;
} pv2_allocation_t;
static inline bool pv2_allocation_write(pv2_allocation_t *allocation, bool retained, bool tearing)
{
    if (allocation->writer || allocation->readers[PV2_REF_COPY] || allocation->readers[PV2_REF_TRANSFER] ||
            (!retained && allocation->readers[PV2_REF_BASELINE]) || (!tearing && allocation->readers[PV2_REF_SCANOUT])) {
        return false;
    }
    allocation->writer = true;
    return true;
}
static inline void pv2_allocation_claim(pv2_allocation_t *allocation)
{
    bool claimed = pv2_allocation_write(allocation, false, false);
    assert(claimed);
    (void)claimed;
}
static inline void pv2_allocation_written(pv2_allocation_t *allocation)
{
    assert(allocation->writer);
    allocation->writer = false;
}
static inline void pv2_allocation_hold(pv2_allocation_t *allocation, pv2_ref_kind_t kind)
{
    assert(!allocation->writer && allocation->readers[kind] != (size_t) -1);
    allocation->readers[kind]++;
}
static inline void PV2_ISR_ATTR pv2_allocation_drop(pv2_allocation_t *allocation, pv2_ref_kind_t kind)
{
    assert(allocation->readers[kind]);
    allocation->readers[kind]--;
}
