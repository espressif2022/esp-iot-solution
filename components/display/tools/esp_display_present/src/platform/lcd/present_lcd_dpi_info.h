/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    unsigned bits_per_pixel;
} present_lcd_dpi_info_t;

/* Optional private BSP binding for a live panel with native DPI draw_bitmap.
 * Report the initialized physical dimensions and input FB format. Return
 * false for unknown handles or if drawing is wrapped or remaps coordinates.
 * The BSP must keep these facts valid while Present borrows the panel. */
bool present_lcd_dpi_get_board_info(esp_lcd_panel_handle_t panel, present_lcd_dpi_info_t *info);
