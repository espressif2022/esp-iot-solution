/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

enum {
#ifdef PRESENT_LVGL_PPA_P4
    PRESENT_LVGL_PPA_WIDTH = 1024,
    PRESENT_LVGL_PPA_HEIGHT = 600,
#else
    PRESENT_LVGL_PPA_WIDTH = 800,
    PRESENT_LVGL_PPA_HEIGHT = 480,
#endif
    PRESENT_LVGL_PPA_STRIDE = PRESENT_LVGL_PPA_WIDTH * 2,
    PRESENT_LVGL_PPA_FRAME_BYTES = PRESENT_LVGL_PPA_STRIDE * PRESENT_LVGL_PPA_HEIGHT,
};

bool present_lvgl_ppa_fill_start(void);
esp_err_t present_lvgl_ppa_fill_stop(void);
void present_lvgl_ppa_fill_set_lease(void *pixels, size_t bytes);
esp_err_t present_lvgl_ppa_fill_error(void);
