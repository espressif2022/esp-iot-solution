/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_records.h"

/* One authority for frame records. All access, including ISR completion, must
 * hold the owning backend lock. No allocation, hardware or callbacks while
 * progressing records. Pending counts accepted/reserved accesses, not images.
 * Output policies still supply presentation evidence; terminal is not present.
 */
typedef enum { PV2_CORE_RUNNING, PV2_CORE_CLOSING, PV2_CORE_QUIESCENT } pv2_core_life_t;
typedef struct {
    pv2_record_t common;
    size_t pending, leased;
    bool sealed, canceled;
} pv2_core_frame_t;
typedef struct {
    pv2_core_frame_t *frames;
    size_t count, active;
    pv2_cancel_history_t canceled;
    pv2_backend_fault_t fault;
    pv2_core_life_t life;
} pv2_frame_core_t;
esp_err_t pv2_frame_core_init(pv2_frame_core_t *core, size_t count);
void pv2_frame_core_deinit(pv2_frame_core_t *core);
pv2_core_frame_t *pv2_frame_core_find(pv2_frame_core_t *core, uint64_t token);
esp_err_t pv2_frame_core_begin(pv2_frame_core_t *core, pv2_frame_t *out);
esp_err_t pv2_frame_core_seal(pv2_frame_core_t *core, pv2_core_frame_t *frame, pv2_receipt_t *out);
void pv2_frame_core_reap(pv2_frame_core_t *core, pv2_core_frame_t *frame);
void pv2_frame_core_complete(pv2_frame_core_t *core, pv2_core_frame_t *frame);
void pv2_frame_core_fail(pv2_frame_core_t *core, pv2_core_frame_t *frame, uint64_t cookie, esp_err_t error);
esp_err_t pv2_frame_core_release(pv2_core_frame_t *frame);
/* Call only after output/copy quiescence. Rejects outstanding access/leases. */
esp_err_t pv2_frame_core_quiescent(pv2_frame_core_t *core);

/* Complete-image policies establish drain explicitly (FB confirmation ends
 * frame receipt ownership, not the separate scanout allocation reference). */
void pv2_frame_core_terminal(pv2_frame_core_t *, pv2_core_frame_t *);
