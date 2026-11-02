/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_te_timing.h"
#include <stddef.h>
pv2_te_window_t pv2_te_window(const pv2_te_timing_t *timing, uint64_t w, uint64_t edge, uint64_t now, bool measured)
{
    pv2_te_window_t out = {.admission = PV2_TE_UNSUPPORTED};
    if (!timing || !timing->period_us || !timing->scan_us || timing->scan_us > timing->period_us || !w ||
            timing->edge_to_scan_us >= timing->period_us || timing->guard_us >= timing->period_us ||
            timing->jitter_us > timing->guard_us) {
        return out;
    }
    uint64_t delta = w > timing->scan_us ? w - timing->scan_us : timing->scan_us - w;
    if (delta >= timing->period_us || timing->guard_us > (timing->period_us - delta) / 2) {
        return out;
    }
    uint64_t lo = (w < timing->scan_us ? delta : 0) + timing->guard_us;
    uint64_t hi = timing->period_us - (w > timing->scan_us ? delta : 0) - timing->guard_us;
    if (lo >= hi) {
        return out;
    }
    out.earliest_phase_us = lo;
    out.latest_phase_us = hi;
    if (!measured) {
        out.admission = PV2_TE_UNPROVEN;
        return out;
    }
    if (now < edge || now - edge >= timing->period_us) {
        out.admission = PV2_TE_WAIT;
        return out;
    }
    uint64_t phase = now - edge;
    phase = phase >= timing->edge_to_scan_us ? phase - timing->edge_to_scan_us : timing->period_us - (timing->edge_to_scan_us - phase);
    out.admission = phase >= lo && phase < hi ? PV2_TE_ALLOW : PV2_TE_WAIT;
    return out;
}
