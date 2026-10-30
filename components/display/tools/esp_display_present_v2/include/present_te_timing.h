/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit linear full-frame model: panel scan and writer traverse rows in the
 * same physical direction. Durations are worst-case bounds, not average FPS.
 * Scan starts edge_to_scan_us after the selected TE edge. No implicit polarity.
 * A segmented/burst writer must NOT use this model without a proven envelope. */
typedef struct {
    uint64_t period_us, scan_us, edge_to_scan_us, guard_us, jitter_us;
} pv2_te_timing_t;
typedef enum { PV2_TE_ALLOW, PV2_TE_WAIT, PV2_TE_UNPROVEN, PV2_TE_UNSUPPORTED } pv2_te_admission_t;
typedef struct {
    pv2_te_admission_t admission;
    uint64_t earliest_phase_us, latest_phase_us;
} pv2_te_window_t;
pv2_te_window_t pv2_te_window(const pv2_te_timing_t *, uint64_t transfer_us,
                              uint64_t edge_us, uint64_t now_us, bool measured);
#ifdef __cplusplus
}
#endif
