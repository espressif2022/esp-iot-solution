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
typedef struct pv2_lcd_scanout pv2_lcd_scanout_t;
typedef enum { PV2_LCD_DSI, PV2_LCD_RGB_BOUNCE } pv2_lcd_scanout_kind_t;
typedef esp_err_t (*pv2_stop_scanout_fn)(void *user, uint32_t timeout_ms);

typedef struct {
    esp_lcd_panel_handle_t panel;
    pv2_lcd_scanout_kind_t kind;
    uint32_t width, height;
    pv2_pixel_format_t format;
    size_t buffer_count; /* 2..3 driver-owned framebuffers. */
    /** RGB requires an actual bounce-buffer panel with no application bounce
     * callback, no driver rotation/mirror, and continuous refresh. Non-bounce
     * EOF is not a reliable retirement event on every IDF/SoC. */
    bool rgb_bounce_buffer_mode;
    bool delete_panel_on_stop; /* Alternative safe stop: transfers panel ownership on attach. */
    /** Unless delete_panel_on_stop: required board-owned safe stop: successful return guarantees scanning
     * stopped AND all registered callbacks exited; framebuffer allocations
     * may be freed after stopping DMA; backend stop does not read them again.
     * May return TIMEOUT and retry.
     * Not a display-off/backlight hook. Driver deletion may block independently
     * of timeout_ms; a bounded board stop hook is needed for a hard deadline. */
    pv2_stop_scanout_fn stop_scanout;
    void *user;
} pv2_lcd_scanout_config_t;

/**
 * @brief Exclusive initialized panel; no previous submissions or callback owner.
 * Driver FB0 is initially scanning. Bind get_buffers output as backend external
 * buffers and set initial_scanout_buffer=FB0. Bus ownership stays with the caller; panel ownership follows
 * delete_panel_on_stop. Driver must use periodic frame_buf_complete events; two events after submit returns fence one
 * already-running ISR that sampled the old framebuffer. Source is a whole exact driver FB, tight/native layout; no copy
 * fallback. COLOR_DONE/raw VSYNC never confirms a frame. Hardware timing still requires board validation; compatibility
 * is with the audited local IDF 6.2 APIs.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_adapter Output adapter.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lcd_scanout_create(const pv2_lcd_scanout_config_t *config, pv2_lcd_scanout_t **out_adapter);

/**
 * @brief Lcd scanout get buffers.
 *
 * @param adapter Panel adapter instance.
 * @param out_buffers Output buffers.
 * @param capacity Number of entries available in out_buffers.
 * @param count Receives the buffer count.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lcd_scanout_get_buffers(pv2_lcd_scanout_t *adapter, void **out_buffers, size_t capacity, size_t *count);

/**
 * @brief Lcd scanout get interface.
 *
 * @param adapter Panel adapter instance.
 * @param interface interface.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lcd_scanout_get_interface(pv2_lcd_scanout_t *adapter, pv2_panel_adapter_t *interface);

/**
 * @brief Backend must first stop/destroy. Caller destroys bus; panel only if not
 * transferred to delete_panel_on_stop.
 *
 * @param adapter Panel adapter instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_lcd_scanout_destroy(pv2_lcd_scanout_t *adapter);
/* Transport constraints only; panel command alignment may be stricter. */
esp_err_t pv2_lcd_scanout_get_output_caps(pv2_lcd_scanout_t *adapter, pv2_output_caps_t *caps);

#ifdef __cplusplus
}
#endif
