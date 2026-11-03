/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_lcd_scanout.h"
/* Before any producer frame; attaches through the existing presenter and
 * fences FB0 selection. Only the factory may submit while this is pending. */
esp_err_t present_lcd_scanout_initialize(esp_display_present_lcd_scanout_t *, uint32_t timeout_ms);

/* Drained presenter only. Changes the tightly packed view, not driver storage
 * or physical panel timings. Pixel count and exact borrowed capacities stay fixed. */
esp_err_t present_lcd_scanout_set_layout(esp_display_present_lcd_scanout_t *, uint32_t, uint32_t);
