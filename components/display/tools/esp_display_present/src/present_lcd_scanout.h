/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_backend.h"
#include "present_output_caps.h"
#include "esp_lcd_panel_ops.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct esp_display_present_lcd_scanout esp_display_present_lcd_scanout_t;
typedef enum {
    ESP_DISPLAY_PRESENT_LCD_DSI,
    ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE,
} esp_display_present_lcd_scanout_kind_t;
typedef bool (*esp_display_present_scanout_vsync_fn)(void *user, uint64_t timestamp_us, bool from_isr);
typedef esp_err_t (*esp_display_present_stop_scanout_fn)(void *user, uint32_t timeout_ms);

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_display_present_lcd_scanout_kind_t kind;
    uint32_t width, height;
    esp_display_present_engine_pixel_format_t format;
    size_t buffer_count; /* 1..3 driver-owned framebuffers; one permits active writes. */
    esp_display_present_byte_order_t byte_order; /* Driver-visible representation; no conversion here. */
    /** RGB requires an actual bounce-buffer panel with no application bounce
     * callback, no driver rotation/mirror, and continuous refresh. Non-bounce
     * EOF is not a reliable retirement event on every IDF/SoC. */
    bool bounce;
    bool single_retire; /* Single-FB exposure only; unsupported for multiple FBs. */
    bool delete_panel; /* Alternative safe stop: transfers panel ownership on attach. */
    /** Unless delete_panel or retain_scanout: required board-owned safe stop: successful return guarantees scanning
     * stopped AND all registered callbacks exited; framebuffer allocations
     * may be freed after stopping DMA; backend stop does not read them again.
     * May return TIMEOUT and retry.
     * Not a display-off/backlight hook. Driver deletion may block independently
     * of timeout_ms; a bounded board stop hook is needed for a hard deadline. */
    esp_display_present_stop_scanout_fn stop_scanout;
    void *user;
    /** FB external driver pool only: final stop drains the pending switch and
     * detaches/joins callbacks, but leaves immutable driver-owned FBs scanning.
     * Mutually exclusive with delete_panel and stop_scanout. Panel and
     * its FB pool must outlive this adapter/backend. No backend-owned pixels
     * may remain visible. Normal presenter drain / V1 quiesce must not call
     * this final-stop path. */
    bool retain_scanout;
    esp_display_present_scanout_vsync_fn on_vsync; /* Raw observation only; ISR-safe, nonblocking; never retirement. */
    void *vsync_user;
} esp_display_present_lcd_scanout_config_t;

/**
 * @brief Exclusive initialized panel; no previous submissions or callback owner.
 * Driver FB0 is initially scanning. Bind get_buffers output as backend external
 * buffers and set scan_buf=FB0. Bus ownership stays with the caller; panel ownership follows
 * delete_panel. Driver must use periodic frame_buf_complete events. A task-context ISR join after submit
 * drains an already-running ISR that sampled the old FB; the next event then confirms the switch. If the join is
 * unavailable or fails, two events remain the fallback. Source is a whole exact driver FB, tight/native layout; no copy
 * fallback. COLOR_DONE/raw VSYNC never confirms a frame. Hardware timing still requires board validation; compatibility
 * is with the audited local IDF 6.2 APIs. The private compatibility factory
 * explicitly fences FB0 before any producer lease when reusing a borrowed panel.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_adapter Output adapter.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_scanout_create(const esp_display_present_lcd_scanout_config_t *config, esp_display_present_lcd_scanout_t **out_adapter);

/**
 * @brief Lcd scanout get buffers.
 *
 * @param adapter Panel adapter instance.
 * @param out_buffers Output buffers.
 * @param capacity Number of entries available in out_buffers.
 * @param count Receives the buffer count.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_scanout_get_buffers(esp_display_present_lcd_scanout_t *adapter, void **out_buffers, size_t capacity, size_t *count);

/**
 * @brief Lcd scanout get interface.
 *
 * @param adapter Panel adapter instance.
 * @param interface interface.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_scanout_get_interface(esp_display_present_lcd_scanout_t *adapter, esp_display_present_panel_adapter_t *interface);

/**
 * @brief Backend must first stop/destroy. Caller destroys bus; panel only if not
 * transferred to delete_panel.
 *
 * @param adapter Panel adapter instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_lcd_scanout_destroy(esp_display_present_lcd_scanout_t *adapter);
/* Transport constraints only; panel command alignment may be stricter. */
esp_err_t esp_display_present_lcd_scanout_get_output_caps(esp_display_present_lcd_scanout_t *adapter, esp_display_present_output_caps_t *caps);

/* Before any producer frame; attaches through the existing presenter and
 * fences FB0 selection. Only the factory may submit while this is pending. */
esp_err_t present_lcd_scanout_initialize(esp_display_present_lcd_scanout_t *, uint32_t timeout_ms);

/* Drained presenter only. Changes the tightly packed view, not driver storage
 * or physical panel timings. Pixel count and exact borrowed capacities stay fixed. */
esp_err_t present_lcd_scanout_set_layout(esp_display_present_lcd_scanout_t *, uint32_t, uint32_t);

#ifdef __cplusplus
}
#endif
