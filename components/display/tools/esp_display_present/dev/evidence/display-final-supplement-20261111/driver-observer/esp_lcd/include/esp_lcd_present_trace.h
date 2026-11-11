/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint32_t requests, events, superseded, active_submits;
    uint32_t target_diffs, unresolved, outside_pool, saved;
    uintptr_t panel, fbs[3], requested, observed;
    uint32_t pending, bounce_bytes, kind;
    uint32_t run_boundaries, underruns;
    uint32_t measured_frames, measured_short, measured_restarts, measuring;
    uint32_t supply_frames, supply_short, supply_restarts, supply_min_eofs, supply_expected;
} esp_lcd_present_trace_snapshot_t;
/* Test observer only: no driver control, ownership decisions or error returns. */
void esp_lcd_present_trace_bind(void *panel, void *const *fbs, unsigned count, unsigned bounce_bytes, unsigned kind);
void esp_lcd_present_trace_request(void *panel, unsigned index);
void esp_lcd_present_trace_event(void *panel, unsigned kind, unsigned expected, int observed, uintptr_t raw_descriptor);
void esp_lcd_present_trace_reset(void);
void esp_lcd_present_trace_snapshot(esp_lcd_present_trace_snapshot_t *out);
void esp_lcd_present_trace_report(const char *mode, unsigned rotation);

void esp_lcd_present_trace_rgb_supply(void *panel, unsigned got, unsigned expected, unsigned restart);

void esp_lcd_present_trace_mark_running(unsigned running);

/* Temporary diagnostic hooks only, no ownership or control changes. */
uintptr_t esp_lcd_present_trace_rgb_source(void *panel, uintptr_t *target, unsigned *position, uintptr_t *raw);
void esp_lcd_present_trace_audit(const void *pixels, unsigned kind);
void esp_lcd_present_trace_audit_reset(void);
void esp_lcd_present_trace_audit_report(const char *mode, unsigned rotation);

uintptr_t esp_lcd_present_trace_dpi_source(void *panel, uintptr_t *target, unsigned *position, uintptr_t *raw);
void esp_lcd_present_trace_boundary(void *panel);
void esp_lcd_present_trace_underrun(void *panel);
