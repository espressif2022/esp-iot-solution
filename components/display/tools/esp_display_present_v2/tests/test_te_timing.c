/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_te_timing.h"
#include <assert.h>
int main(void)
{
    pv2_te_timing_t m = {.period_us = 100, .scan_us = 90, .guard_us = 2, .jitter_us = 1};
    for (unsigned w = 1; w < 200; w++)
        for (unsigned t = 0; t < 100; t++) {
            pv2_te_window_t a = pv2_te_window(&m, w, 1000, 1000 + t, true);
            if (a.admission == PV2_TE_ALLOW) {
                /* Every scanline must be written after this refresh's read and
                 * before the next refresh's read, including both endpoints. */
                for (unsigned y = 0; y <= 100; y++) {
                    long write = (long)t * 100 + (long)y * w;
                    long scan = (long)y * m.scan_us;
                    assert(write >= scan + 200);
                    assert(write < scan + 10000 - 200);
                }
            }
        }
    assert(pv2_te_window(&m, 40, 0, 60, false).admission == PV2_TE_UNPROVEN);
    assert(pv2_te_window(&m, 40, 0, 100, true).admission == PV2_TE_WAIT);
    assert(pv2_te_window(&m, 200, 0, 10, true).admission == PV2_TE_UNSUPPORTED);
    assert(pv2_te_window(&m, 40, 0, 55, true).admission == PV2_TE_ALLOW);
    assert(pv2_te_window(&m, 120, 0, 3, true).admission == PV2_TE_ALLOW);
    m.edge_to_scan_us = 20;
    assert(pv2_te_window(&m, 40, 0, 75, true).admission == PV2_TE_ALLOW);
    return 0;
}
