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
typedef struct esp_display_present_lcd_fb_update esp_display_present_lcd_fb_update_t;
typedef enum {
    ESP_DISPLAY_PRESENT_LCD_FB_UPDATE_RGB,
    ESP_DISPLAY_PRESENT_LCD_FB_UPDATE_DSI,
} esp_display_present_lcd_fb_update_kind_t;
typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_display_present_lcd_fb_update_kind_t kind;
    uint32_t width, height;
    esp_display_present_engine_pixel_format_t format;
    esp_display_present_byte_order_t byte_order;
} esp_display_present_lcd_fb_update_config_t;

esp_err_t esp_display_present_lcd_fb_update_create(const esp_display_present_lcd_fb_update_config_t *config,
                                                   esp_display_present_lcd_fb_update_t **out);
esp_err_t esp_display_present_lcd_fb_update_get_interface(esp_display_present_lcd_fb_update_t *adapter,
                                                          esp_display_present_panel_adapter_t *out);
esp_err_t esp_display_present_lcd_fb_update_get_output_caps(esp_display_present_lcd_fb_update_t *adapter,
                                                            esp_display_present_output_caps_t *out);
esp_err_t esp_display_present_lcd_fb_update_destroy(esp_display_present_lcd_fb_update_t *adapter);
