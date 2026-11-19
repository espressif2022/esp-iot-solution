/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Minimal esp_lcd_touch surface for the ESP touch-adapter host test. */
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct esp_lcd_touch_stub_t *esp_lcd_touch_handle_t;
typedef void (*esp_lcd_touch_interrupt_callback_t)(
    esp_lcd_touch_handle_t tp);

#define GPIO_NUM_NC (-1)

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;
    uint8_t track_id;
} esp_lcd_touch_point_data_t;

typedef struct {
    int int_gpio_num;
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
    void *user_data;
} esp_lcd_touch_config_t;

struct esp_lcd_touch_stub_t {
    esp_lcd_touch_config_t config;
    esp_lcd_touch_point_data_t point;
    esp_lcd_touch_point_data_t point2;
    esp_err_t read_result;
    esp_err_t interrupt_register_result;
    esp_err_t interrupt_unregister_result;
    uint32_t read_count;
    uint8_t count;
};

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t tp,
                                 esp_lcd_touch_point_data_t *points,
                                 uint8_t *point_num,
                                 uint8_t max_point_num);
esp_err_t esp_lcd_touch_register_interrupt_callback(
    esp_lcd_touch_handle_t tp,
    esp_lcd_touch_interrupt_callback_t callback);
esp_err_t esp_lcd_touch_register_interrupt_callback_with_data(
    esp_lcd_touch_handle_t tp,
    esp_lcd_touch_interrupt_callback_t callback,
    void *user_data);
