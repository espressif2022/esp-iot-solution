/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "present_te_timing.h"

/* Return the next opening of this measured edge's window, if it lies later
 * in the same edge-to-edge period. If false, wait for a real subsequent edge:
 * a predicted future period must not be treated as a measured edge. The
 * absolute timestamp is written only on success. `window` must be the WAIT
 * result returned for these same timing, edge and now values. */
bool present_te_next_open(const esp_display_present_te_timing_t *timing,
                          const esp_display_present_te_window_t *window,
                          uint64_t edge_us, uint64_t now_us, uint64_t *next_us);
