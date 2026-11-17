/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
/** @brief Internal compile-time bounds and allocation alignment policy (not cache-line size). */
enum {
    PRESENT_BUFFER_ALIGNMENT = 64, /**< Default buffer base alignment in bytes. */
    PRESENT_MAX_FRAMES = 32,       /**< Maximum concurrent frame/receipt records. */
    PRESENT_MAX_DAMAGE_AREAS = 32, /**< Maximum damage areas per frame. */
    PRESENT_MAX_TILE_BUFFERS = 16, /**< Maximum partition tile slots. */
    PRESENT_MAX_COPY_JOBS = 64,    /**< Maximum queued copy jobs. */
};
