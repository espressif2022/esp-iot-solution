/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "backend/present_output_caps.h"
#include "esp_lcd_panel_ops.h"
#ifdef __cplusplus
extern "C" {
#endif
/** @brief Opaque framebuffer scanout adapter handle. */
typedef struct esp_display_present_lcd_fb_scanout esp_display_present_lcd_fb_scanout_t;

/** @brief Scanout panel transport kind. */
typedef enum {
    ESP_DISPLAY_PRESENT_LCD_FB_SCANOUT_DSI, /**< MIPI-DSI video-mode panel. */
    ESP_DISPLAY_PRESENT_LCD_FB_SCANOUT_RGB, /**< RGB interface panel. */
} esp_display_present_lcd_fb_scanout_kind_t;

/** @brief Raw VSYNC observation callback (ISR-safe, nonblocking; never implies retirement). */
typedef bool (*esp_display_present_scanout_vsync_fn)(void *user, uint64_t timestamp_us, bool from_isr);
/** @brief Optional board-owned safe scanout stop callback. */
typedef esp_err_t (*esp_display_present_stop_scanout_fn)(void *user, uint32_t timeout_ms);

/** @brief Framebuffer scanout adapter creation configuration. */
typedef struct {
    esp_lcd_panel_handle_t panel;                    /**< Borrowed initialized panel handle. */
    esp_display_present_lcd_fb_scanout_kind_t kind;  /**< Panel scanout transport kind. */
    uint32_t width;                                  /**< Physical panel width in pixels. */
    uint32_t height;                                 /**< Physical panel height in pixels. */
    esp_display_present_engine_pixel_format_t format;/**< Panel pixel format. */
    size_t buffer_count;                             /**< Driver-owned framebuffer count (1..3). */
    esp_display_present_byte_order_t byte_order;     /**< Driver-visible byte order (no conversion). */
    bool bounce;                                     /**< Legacy confirmed-bounce fallback when driver facts are absent. */
    bool single_retire;                              /**< Single-FB exposure only; unsupported for multiple FBs. */
    bool delete_panel;                               /**< Alternative safe stop: transfer panel ownership on attach. */
    esp_display_present_stop_scanout_fn stop_scanout;/**< Board-owned safe stop hook unless @c delete_panel or @c retain_scanout is set. */
    void *user;                                      /**< User context passed to @c stop_scanout. */
    bool retain_scanout;                             /**< Keep immutable driver-owned FBs scanning after final stop (exclusive with delete_panel/stop_scanout). */
    esp_display_present_scanout_vsync_fn on_vsync;   /**< Optional raw VSYNC observation callback. */
    void *vsync_user;                                /**< User context passed to @c on_vsync. */
} esp_display_present_lcd_fb_scanout_config_t;

/**
 * @brief Exclusive initialized panel; no previous submissions or callback owner.
 * Driver FB0 is initially scanning. Bind get_buffers output as backend external
 * buffers and set scan_buf=FB0. Bus ownership stays with the caller; panel ownership follows
 * delete_panel. Periodic completion uses an ISR join after submit (two events if
 * joining fails). Requested RGB link-switch completion instead preserves an
 * event received during submission and publishes it only after draw succeeds.
 * One outstanding transaction and exclusive panel ownership are mandatory;
 * local cookies do not add framebuffer identity to IDF's untagged callbacks. Source is a whole exact driver FB, tight/native layout; no copy
 * fallback. COLOR_DONE/raw VSYNC never confirms a frame. Hardware timing still requires board validation; compatibility
 * is with the audited local IDF 6.2 APIs. The private compatibility factory
 * explicitly fences FB0 before any producer lease when reusing a borrowed panel.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_adapter Output adapter.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_fb_scanout_create(const esp_display_present_lcd_fb_scanout_config_t *config, esp_display_present_lcd_fb_scanout_t **out_adapter);

/**
 * @brief Lcd scanout get buffers.
 *
 * @param adapter Panel adapter instance.
 * @param out_buffers Output buffers.
 * @param capacity Number of entries available in out_buffers.
 * @param count Receives the buffer count.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_fb_scanout_get_buffers(esp_display_present_lcd_fb_scanout_t *adapter, void **out_buffers, size_t capacity, size_t *count);

/**
 * @brief Lcd scanout get interface.
 *
 * @param adapter Panel adapter instance.
 * @param interface interface.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_fb_scanout_get_interface(esp_display_present_lcd_fb_scanout_t *adapter, esp_display_present_panel_adapter_t *interface);

/**
 * @brief Backend must first stop/destroy. Caller destroys bus; panel only if not
 * transferred to delete_panel.
 *
 * @param adapter Panel adapter instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_fb_scanout_destroy(esp_display_present_lcd_fb_scanout_t *adapter);
/** @brief Query transport alignment and memory constraints for the scanout adapter. */
esp_err_t esp_display_present_lcd_fb_scanout_get_output_caps(esp_display_present_lcd_fb_scanout_t *adapter, esp_display_present_output_caps_t *caps);

/** @brief Fence initial FB0 selection before any producer frame is admitted. */
esp_err_t present_lcd_fb_scanout_initialize(esp_display_present_lcd_fb_scanout_t *, uint32_t timeout_ms);

/** @brief Read existing driver framebuffers without registering callbacks, submitting DMA, or allocating. */
esp_err_t present_lcd_fb_scanout_probe_buffers(esp_lcd_panel_handle_t panel,
                                               esp_display_present_lcd_fb_scanout_kind_t kind,
                                               void **buffers, size_t *count);

#ifdef __cplusplus
}
#endif
