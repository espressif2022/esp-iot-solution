/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ESP_COLOR_FOURCC_RGB16 0
#define ESP_COLOR_FOURCC_BGR24 1
typedef void *async_color_convert_handle_t;
typedef struct { int unused; } async_color_convert_event_data_t;
typedef bool (*async_color_convert_isr_cb_t)(async_color_convert_handle_t, async_color_convert_event_data_t *, void *);
typedef struct {
    uint32_t backlog;
    size_t dma_burst_size;
    uint32_t intr_priority;
} async_color_convert_config_t;
typedef struct {
    const void *src_buffer;
    uint32_t src_stride, src_height, src_x, src_y;
    void *dst_buffer;
    uint32_t dst_stride, dst_height, dst_x, dst_y, copy_width, copy_height;
    int src_color_format, dst_color_format, color_conv_std;
} async_color_convert_request_t;
esp_err_t esp_async_color_convert_install_dma2d(const async_color_convert_config_t *, async_color_convert_handle_t *);
esp_err_t esp_async_color_convert_uninstall(async_color_convert_handle_t);
esp_err_t esp_async_color_convert(async_color_convert_handle_t, const async_color_convert_request_t *, async_color_convert_isr_cb_t, void *);
