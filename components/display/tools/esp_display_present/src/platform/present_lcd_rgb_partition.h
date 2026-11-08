/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "backend/present_output_caps.h"
#include "esp_lcd_panel_ops.h"

typedef struct esp_display_present_lcd_rgb_partition esp_display_present_lcd_rgb_partition_t;
typedef enum {
    ESP_DISPLAY_PRESENT_LCD_PARTITION_RGB,
    ESP_DISPLAY_PRESENT_LCD_PARTITION_DSI,
} esp_display_present_lcd_partition_kind_t;
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_display_present_lcd_partition_kind_t kind;
    uint32_t width, height;
    esp_display_present_engine_pixel_format_t format;
    esp_display_present_byte_order_t byte_order;
} esp_display_present_lcd_rgb_partition_config_t;

esp_err_t esp_display_present_lcd_rgb_partition_create(const esp_display_present_lcd_rgb_partition_config_t *config,
                                                        esp_display_present_lcd_rgb_partition_t **out);
esp_err_t esp_display_present_lcd_rgb_partition_get_interface(esp_display_present_lcd_rgb_partition_t *adapter,
                                                               esp_display_present_panel_adapter_t *out);
esp_err_t esp_display_present_lcd_rgb_partition_get_output_caps(esp_display_present_lcd_rgb_partition_t *adapter,
                                                                 esp_display_present_output_caps_t *out);
esp_err_t esp_display_present_lcd_rgb_partition_destroy(esp_display_present_lcd_rgb_partition_t *adapter);
