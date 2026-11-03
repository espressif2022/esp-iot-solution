/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Source-only compatibility; does not provide pv2_* ABI symbols. */
#pragma once
#include "present_pv2_compat.h"
#include "present_lvgl.h"

#define pv2_lvgl esp_lv_present_native
#define pv2_lvgl_create esp_lv_present_native_create
#define pv2_lvgl_create_pooled esp_lv_present_native_create_pooled
#define pv2_lvgl_create_zero_copy esp_lv_present_native_create_zero_copy
#define pv2_lvgl_destroy esp_lv_present_native_destroy
#define pv2_lvgl_get_display esp_lv_present_native_get_display
#define pv2_lvgl_get_error esp_lv_present_native_get_error
#define pv2_lvgl_set_rotation esp_lv_present_native_set_rotation
#define pv2_lvgl_t esp_lv_present_native_t
