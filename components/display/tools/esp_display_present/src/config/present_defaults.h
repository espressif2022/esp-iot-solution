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
/** @brief Fixed backend priority override; zero snapshots the bound producer priority. */
#ifndef CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY
#define CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY 0
#endif
/** @brief Default partition input tile byte target (matches Kconfig; overridden by explicit builds). */
#ifndef CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES
#define CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES 32768
#endif
/** @brief Default Panel IO staging slot byte budget (hard limit). */
#ifndef CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES
#ifdef CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES
#define CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES
#else
#define CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES 16384
#endif
#endif

/** @brief Compute default tile row count from row stride and screen height (minimum 1 usable row). */
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

/** @brief Compute bounded Panel IO staging slot size in whole physical rows (0 if one row exceeds budget). */
static inline size_t present_default_io_stage_bytes(size_t row_bytes, size_t height)
{
    if (!row_bytes) {
        return 0;
    }
    size_t rows = CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES / row_bytes;
    return row_bytes * (rows < height ? rows : height);
}
