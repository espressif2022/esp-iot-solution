/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
/* Allocation policy, NOT a substitute for the platform cache-line size. */
enum {
    PV2_BUFFER_ALIGNMENT = 64,
    PV2_MAX_FRAMES = 32,
    PV2_MAX_DAMAGE_AREAS = 32,
    PV2_MAX_TILE_BUFFERS = 16,
    PV2_MAX_COPY_JOBS = 64,
};
