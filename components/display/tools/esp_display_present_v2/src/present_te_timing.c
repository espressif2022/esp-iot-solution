/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_te_timing.h"
#include <stddef.h>
pv2_te_window_t pv2_te_window(const pv2_te_timing_t *m, uint64_t w, uint64_t edge, uint64_t now, bool measured)
{
    pv2_te_window_t out = {.admission = PV2_TE_UNSUPPORTED};
    if (!m || !m->period_us || !m->scan_us || m->scan_us > m->period_us || !w ||
            m->edge_to_scan_us >= m->period_us || m->guard_us >= m->period_us ||
            m->jitter_us > m->guard_us) {
        return out;
    }
    uint64_t delta = w > m->scan_us ? w - m->scan_us : m->scan_us - w;
    if (delta >= m->period_us || m->guard_us > (m->period_us - delta) / 2) {
        return out;
    }
    uint64_t lo = (w < m->scan_us ? delta : 0) + m->guard_us;
    uint64_t hi = m->period_us - (w > m->scan_us ? delta : 0) - m->guard_us;
    if (lo >= hi) {
        return out;
    }
    out.earliest_phase_us = lo;
    out.latest_phase_us = hi;
    if (!measured) {
        out.admission = PV2_TE_UNPROVEN;
        return out;
    }
    if (now < edge || now - edge >= m->period_us) {
        out.admission = PV2_TE_WAIT;
        return out;
    }
    uint64_t phase = now - edge;
    phase = phase >= m->edge_to_scan_us ? phase - m->edge_to_scan_us : m->period_us - (m->edge_to_scan_us - phase);
    out.admission = phase >= lo && phase < hi ? PV2_TE_ALLOW : PV2_TE_WAIT;
    return out;
}
