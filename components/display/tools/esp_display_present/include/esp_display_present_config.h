/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_display_present_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum supported driver framebuffers per panel. */
#define ESP_DISPLAY_PRESENT_MAX_FRAME_BUFFERS 3U

/** @brief Panel transport category. */
typedef enum {
    ESP_DISPLAY_PRESENT_PANEL_AUTO = 0,  /**< Infer panel transport from handles (Panel-IO when io is set). */
    ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI,  /**< MIPI-DSI video-mode framebuffer panel. */
    ESP_DISPLAY_PRESENT_PANEL_RGB,       /**< RGB interface framebuffer panel. */
    ESP_DISPLAY_PRESENT_PANEL_IO,        /**< Panel-IO / GRAM command-interface panel (SPI, QSPI, I80). */
} esp_display_present_panel_t;

/** @brief Public presentation mode requested by the caller. */
typedef enum {
    ESP_DISPLAY_PRESENT_MODE_NONE = 0,       /**< Panel-IO direct transfer without tearing avoidance. */
    ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL,    /**< Double buffering with full frame buffers. */
    ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL,    /**< Triple buffering with full frame buffers. */
    ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT,  /**< Double buffering with direct dirty-area rendering into full buffers. */
    ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, /**< Triple buffering with partial/partition refresh. */
    ESP_DISPLAY_PRESENT_MODE_TE_SYNC,        /**< TE GPIO synchronized transfer for SPI/I80/QSPI panels. */
    ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, /**< Double buffering with partial/partition refresh. */
    ESP_DISPLAY_PRESENT_MODE_AUTO,           /**< Resolve FB 2/3 buffers to PARTIAL, or Panel-IO by TE config. */
} esp_display_present_mode_t;

/**
 * @brief Optional panel TE synchronization settings.
 *
 * A zeroed config is disabled; GPIO 0 is valid, so set @c te_enabled when a TE
 * line is wired. Frame/blanking/transfer timing is measured at runtime; @c
 * bus_freq_hz and @c data_lines only seed the first-frame estimate.
 */
typedef struct {
    int gpio_num;         /**< TE input GPIO number, or -1 when disabled. */
    uint32_t bus_freq_hz; /**< Initial panel-bus frequency estimate in hertz. */
    uint8_t data_lines;   /**< Number of panel-bus data lines. */
} esp_display_present_te_sync_config_t;

/** @brief Initializer for a disabled TE sync configuration. */
#define ESP_DISPLAY_PRESENT_TE_SYNC_DISABLED() \
    (esp_display_present_te_sync_config_t) { .gpio_num = -1 }

/**
 * @brief Panel and bus hardware descriptor (borrowed handles must outlive presenter deletion).
 *
 * RGB and MIPI-DSI share generic handle shapes, so set @c panel_type explicitly
 * for them; Panel-IO may use AUTO when @c io is supplied.
 */
typedef struct {
    esp_lcd_panel_handle_t panel; /**< Borrowed initialized panel handle. */
    esp_lcd_panel_io_handle_t io; /**< Required for panel-IO/GRAM transfers; otherwise optional. */
    esp_display_present_panel_t panel_type; /**< Panel transport category. */
    esp_display_present_pixel_format_t input_pixel_format; /**< Pixel format accepted by the initialized panel data path. */
    esp_display_present_rotation_t rotation; /**< Logical-to-physical rotation. */
    bool swap_bytes; /**< Panel-IO RGB565 wire byte swap; leave false for native RGB/MIPI layouts. */
    bool te_enabled; /**< Whether TE synchronization is enabled. */
    esp_display_present_te_sync_config_t te_sync; /**< TE synchronization settings. */
    bool spi_psram_dma_direct; /**< Must match SPI IO creation flag; fallback when driver memory query is unavailable. */
} esp_display_present_hw_config_t;

/**
 * @brief Frame-buffer presentation profile for RGB and MIPI-DSI paths.
 *
 * Borrowed @c frame_buffers must outlive the presenter and satisfy the driver's
 * DMA/cache alignment contract; a zero @c frame_buffer_count queries the driver pool.
 */
typedef struct {
    esp_display_present_mode_t mode; /**< Requested presentation mode. */
    void *frame_buffers[ESP_DISPLAY_PRESENT_MAX_FRAME_BUFFERS]; /**< Borrowed framebuffer addresses. */
    uint8_t frame_buffer_count; /**< Number of valid framebuffer addresses. */
} esp_display_present_fb_config_t;

/**
 * @brief Producer draw-buffer policy (tile pool in PARTITION modes, plus TE compose count).
 */
typedef struct {
    uint16_t lines;   /**< Input tile rows; 0 uses the configured default byte target. */
    uint8_t buffers;  /**< Input tile count (1 or 2; 0 selects automatically with 2->1 fallback). */
    bool in_psram;    /**< Prefer PSRAM for input tiles when available. */
    uint8_t te_compose_buffers; /**< TE_SYNC full-screen compose buffers (0/1 single, 2 pipelined). */
} esp_display_present_drawbuf_config_t;

/** @brief Pixel processing and driver submission context for Panel-IO NONE. */
typedef enum {
    ESP_DISPLAY_PRESENT_IO_SUBMIT_DEFAULT = 0, /**< Caller processing/submission (same as CALLER). */
    ESP_DISPLAY_PRESENT_IO_SUBMIT_CALLER,      /**< Process pixels and submit from the producer task; may block. */
    ESP_DISPLAY_PRESENT_IO_SUBMIT_BACKGROUND,  /**< Process pixels and submit from one backend task; priority follows component task policy. */
} esp_display_present_io_submit_t;

/** @brief Target hardware, presentation policy and endpoint memory configuration. */
typedef struct {
    esp_display_present_hw_config_t hw; /**< Panel and bus configuration. */
    esp_display_present_fb_config_t fb; /**< Framebuffer presentation policy. */
    esp_display_present_drawbuf_config_t drawbuf; /**< Partition draw-buffer policy. */
    esp_display_present_io_submit_t io_submit; /**< Panel-IO NONE processing/submission task policy. */
} esp_display_present_target_config_t;

#ifdef __cplusplus
}
#endif
