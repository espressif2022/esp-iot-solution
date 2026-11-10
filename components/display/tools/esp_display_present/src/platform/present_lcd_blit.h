/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "backend/present_output_caps.h"
#include "esp_lcd_panel_ops.h"

/* RGB/DPI region draw adapter. on_color_trans_done releases the source tile;
 * it does not retire a scanout framebuffer or establish tear-free display.
 * The public RGB/DSI NONE path uses this adapter, not FB PARTIAL modes. */
typedef struct esp_display_present_lcd_blit esp_display_present_lcd_blit_t;
typedef enum {
    ESP_DISPLAY_PRESENT_LCD_BLIT_RGB,
    ESP_DISPLAY_PRESENT_LCD_BLIT_DSI,
} esp_display_present_lcd_blit_kind_t;
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_display_present_lcd_blit_kind_t kind;
    uint32_t width, height;
    esp_display_present_engine_pixel_format_t format;
    esp_display_present_byte_order_t byte_order;
} esp_display_present_lcd_blit_config_t;

esp_err_t esp_display_present_lcd_blit_create(const esp_display_present_lcd_blit_config_t *config,
                                              esp_display_present_lcd_blit_t **out);
esp_err_t esp_display_present_lcd_blit_get_interface(esp_display_present_lcd_blit_t *adapter,
                                                     esp_display_present_panel_adapter_t *out);
esp_err_t esp_display_present_lcd_blit_get_output_caps(esp_display_present_lcd_blit_t *adapter,
                                                       esp_display_present_output_caps_t *out);
esp_err_t esp_display_present_lcd_blit_destroy(esp_display_present_lcd_blit_t *adapter);
