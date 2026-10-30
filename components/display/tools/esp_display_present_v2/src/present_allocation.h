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
static inline bool pv2_allocation_write(pv2_allocation_t *a, bool retained, bool tearing)
{
    if (a->writer || a->readers[PV2_REF_COPY] || a->readers[PV2_REF_TRANSFER] ||
            (!retained && a->readers[PV2_REF_BASELINE]) || (!tearing && a->readers[PV2_REF_SCANOUT])) {
        return false;
    }
    a->writer = true;
    return true;
}
static inline void pv2_allocation_claim(pv2_allocation_t *a)
{
    bool claimed = pv2_allocation_write(a, false, false);
    assert(claimed);
    (void)claimed;
}
static inline void pv2_allocation_written(pv2_allocation_t *a)
{
    assert(a->writer);
    a->writer = false;
}
static inline void pv2_allocation_hold(pv2_allocation_t *a, pv2_ref_kind_t kind)
{
    assert(!a->writer && a->readers[kind] != (size_t) -1);
    a->readers[kind]++;
}
static inline void PV2_ISR_ATTR pv2_allocation_drop(pv2_allocation_t *a, pv2_ref_kind_t kind)
{
    assert(a->readers[kind]);
    a->readers[kind]--;
}
