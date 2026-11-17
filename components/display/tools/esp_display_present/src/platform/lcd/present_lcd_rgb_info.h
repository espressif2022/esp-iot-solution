/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"
/**
 * @brief Private platform/BSP RGB panel description (never encodes policy preferences).
 */
typedef struct {
    uint32_t width;                 /**< Panel width in pixels. */
    uint32_t height;                /**< Panel height in pixels. */
    size_t frame_buffer_count;      /**< Driver-allocated framebuffer count. */
    size_t frame_buffer_bytes;      /**< Size of each framebuffer in bytes. */
    size_t bounce_buffer_bytes;     /**< Bounce buffer size in bytes (0 when unused). */
    unsigned bits_per_pixel;        /**< Panel bits per pixel. */
    bool continuous_refresh;        /**< True when continuous RGB scanout is active. */
    bool custom_bounce_callback;    /**< True when custom bounce callback is registered. */
    bool transformed;               /**< True when driver-level transform is enabled. */
} present_lcd_rgb_info_t;

/** @brief Authoritative RGB panel facts paired with safe-retirement support flag. */
typedef struct {
    present_lcd_rgb_info_t panel; /**< Initialized RGB panel parameters. */
    bool retirement;              /**< True if safe framebuffer retirement semantics are guaranteed. */
} present_lcd_rgb_facts_t;

/** @brief Query RGB panel facts (driver query takes precedence; ESP_ERR_NOT_FOUND when unavailable). */
esp_err_t present_lcd_rgb_get_facts(esp_lcd_panel_handle_t panel, present_lcd_rgb_facts_t *facts);
/** @brief Optional BSP hook providing RGB panel info when driver query is unavailable. */
esp_err_t present_lcd_rgb_get_board_info(esp_lcd_panel_handle_t panel, present_lcd_rgb_info_t *info);
