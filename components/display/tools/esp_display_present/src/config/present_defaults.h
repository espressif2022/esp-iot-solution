/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifndef PRESENT_HOST
#include "sdkconfig.h"
#endif
/* Host defaults match Kconfig. Explicit build definitions override these. */
#ifndef CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES
#define CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES 32768
#endif
#ifndef CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES
#define CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES 16384
#endif

/* Input target is soft: one logical row is the minimum usable tile. */
static inline size_t present_default_tile_rows(size_t row_bytes, size_t height)
{
    if (!row_bytes || !height) {
        return 0;
    }
    size_t rows = CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES / row_bytes;
    if (!rows) {
        rows = 1;
    }
    return rows < height ? rows : height;
}

/* Transport limit is hard: never silently enlarge a staging slot. */
static inline size_t present_default_te_stage_bytes(size_t row_bytes, size_t height)
{
    if (!row_bytes) {
        return 0;
    }
    size_t rows = CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES / row_bytes;
    return row_bytes * (rows < height ? rows : height);
}
