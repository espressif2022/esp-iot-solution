/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Internal engine pixel format. */
typedef enum {
    ESP_DISPLAY_PRESENT_FORMAT_RGB565, /**< 16-bit RGB565. */
    ESP_DISPLAY_PRESENT_FORMAT_RGB888, /**< 24-bit RGB888. */
} esp_display_present_engine_pixel_format_t;

/**
 * @brief Byte order of a multi-byte pixel scalar (not RGB/BGR channel order).
 *
 * RGB888 uses R,G,B byte sequence; only NATIVE is valid for RGB888.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_ORDER_NATIVE,  /**< Native host/panel byte order. */
    ESP_DISPLAY_PRESENT_ORDER_SWAPPED, /**< Byte-swapped 16-bit pixel scalar. */
} esp_display_present_byte_order_t;

/** @brief Clockwise logical-to-physical engine rotation. */
typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0,   /**< No rotation. */
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90,  /**< 90 degrees clockwise. */
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_180, /**< 180 degrees clockwise. */
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270, /**< 270 degrees clockwise. */
} esp_display_present_engine_rotation_t;

/** @brief Half-open pixel rectangle (x/y are inclusive origins; width/height are extents). */
typedef struct {
    uint32_t x;      /**< Origin X coordinate in pixels. */
    uint32_t y;      /**< Origin Y coordinate in pixels. */
    uint32_t width;  /**< Width in pixels. */
    uint32_t height; /**< Height in pixels. */
} esp_display_present_rect_t;

/** @brief Surface memory layout descriptor. */
typedef struct {
    uint32_t width;  /**< Width in pixels. */
    uint32_t height; /**< Height in pixels. */
    size_t stride_bytes; /**< Row stride in bytes. */
    esp_display_present_engine_pixel_format_t format; /**< Pixel format. */
    esp_display_present_byte_order_t byte_order;      /**< Pixel scalar byte order. */
} esp_display_present_layout_t;

/** @brief Bounded pixel buffer surface. */
typedef struct {
    void *pixels;                        /**< Backing pixel memory pointer. */
    size_t capacity_bytes;               /**< Total writable/readable capacity in bytes. */
    esp_display_present_layout_t layout; /**< Surface dimensions, stride, format, and byte order. */
} esp_display_present_surface_t;

#ifdef __cplusplus
}
#endif
