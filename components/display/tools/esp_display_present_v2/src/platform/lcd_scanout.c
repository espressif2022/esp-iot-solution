/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lcd_scanout.h"
#include "present_os.h"
#include "esp_cache.h"
#include <limits.h>
#ifdef PV2_HOST
#define PV2_DSI_SUPPORTED 1
#define PV2_RGB_SUPPORTED 1
#else
#include "soc/soc_caps.h"
#define PV2_DSI_SUPPORTED SOC_MIPI_DSI_SUPPORTED
#define PV2_RGB_SUPPORTED SOC_LCD_RGB_SUPPORTED
#endif
#if PV2_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif
#if PV2_RGB_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif
struct pv2_lcd_scanout {
    pv2_lock_t lock;
    pv2_lcd_scanout_config_t config;
    void *buffers[3];
    pv2_panel_event_fn callback;
    void *callback_user;
    uint64_t cookie;
    unsigned boundaries;
    size_t active_callbacks;
    bool attached, armed, closing, source_stopped, stopped;
};
static bool PV2_ISR_ATTR frame_boundary(pv2_lcd_scanout_t *scanout)
{
    pv2_lock_enter(&scanout->lock, true);
    pv2_panel_event_fn callback = NULL;
    void *user = NULL;
    uint64_t cookie = 0;
    unsigned needed = scanout->config.single_boundary_retire ? 1u : 2u;
    if (scanout->attached && scanout->armed && !scanout->closing && ++scanout->boundaries == needed) {
        cookie = scanout->cookie;
        scanout->cookie = 0;
        scanout->armed = false;
        callback = scanout->callback;
        user = scanout->callback_user;
        ++scanout->active_callbacks;
    }
    pv2_lock_exit(&scanout->lock, true);
    if (!callback) {
        return false;
    }
    pv2_backend_event_t event = {.kind = PV2_EVENT_SCANOUT_CONFIRMED, .transaction_cookie = cookie};
    bool need_yield = callback(user, &event, true);
    pv2_lock_enter(&scanout->lock, true);
    --scanout->active_callbacks;
    pv2_lock_exit(&scanout->lock, true);
    return need_yield;
}
#if PV2_DSI_SUPPORTED
static bool PV2_ISR_ATTR dsi_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return frame_boundary(user);
}
#endif
#if PV2_RGB_SUPPORTED
static bool PV2_ISR_ATTR rgb_done(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return frame_boundary(user);
}
#endif
static esp_err_t attach(void *context, pv2_panel_event_fn callback, void *user)
{
    pv2_lcd_scanout_t *scanout = context;
    if (!scanout || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->attached || scanout->closing) {
        return ESP_ERR_INVALID_STATE;
    }
    scanout->callback = callback;
    scanout->callback_user = user;
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PV2_DSI_SUPPORTED
    if (scanout->config.kind == PV2_LCD_DSI) {
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = dsi_done};
        error = esp_lcd_dpi_panel_register_event_callbacks(scanout->config.panel, &callbacks, scanout);
    }
#endif
#if PV2_RGB_SUPPORTED
    if (scanout->config.kind == PV2_LCD_RGB_BOUNCE) {
        esp_lcd_rgb_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = rgb_done};
        error = esp_lcd_rgb_panel_register_event_callbacks(scanout->config.panel, &callbacks, scanout);
    }
#endif
    pv2_lock_enter(&scanout->lock, false);
    if (error == ESP_OK) {
        scanout->attached = true;
    }
    pv2_lock_exit(&scanout->lock, false);
    return error;
}

static pv2_tx_result_t submit(void *context, const pv2_surface_t *surface, const pv2_rect_t *area, uint64_t cookie)
{
    pv2_lcd_scanout_t *scanout = context;
    if (!scanout || !surface || !area || !cookie || area->x || area->y || area->width != scanout->config.width ||
            area->height != scanout->config.height || surface->layout.width != area->width ||
            surface->layout.height != area->height || surface->layout.format != scanout->config.format ||
            surface->layout.byte_order != PV2_ORDER_NATIVE)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    size_t bytes = (size_t)scanout->config.width * scanout->config.height * (scanout->config.format == PV2_FORMAT_RGB565 ? 2 : 3);
    if (surface->layout.stride_bytes != bytes / scanout->config.height || surface->capacity_bytes < bytes)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_ERR_INVALID_SIZE
    };
    bool known = false;
    for (size_t i = 0; i < scanout->config.buffer_count; ++i) {
        known |= scanout->buffers[i] == surface->pixels;
    }
    if (!known)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    pv2_lock_enter(&scanout->lock, false);
    bool busy = !scanout->attached || scanout->closing || scanout->cookie;
    pv2_lock_exit(&scanout->lock, false);
    if (busy)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_ERR_INVALID_STATE
    };
    esp_err_t error =
        esp_cache_msync(surface->pixels, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, error
    };
    pv2_lock_enter(&scanout->lock, false);
    scanout->cookie = cookie;
    scanout->armed = false;
    pv2_lock_exit(&scanout->lock, false);
    error = esp_lcd_panel_draw_bitmap(scanout->config.panel, 0, 0, area->width, area->height, surface->pixels);
    pv2_lock_enter(&scanout->lock, false);
    scanout->boundaries = 0;
    scanout->armed = error == ESP_OK;
    if (error != ESP_OK) {
        scanout->closing = true;
    }
    pv2_lock_exit(&scanout->lock, false);
    return (pv2_tx_result_t) {
        error == ESP_OK ? PV2_TX_ACCEPTED : PV2_TX_UNCERTAIN, error
    };
}

static esp_err_t quiesce(void *context, uint32_t timeout)
{
    pv2_lcd_scanout_t *scanout = context;
    if (!scanout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->stopped) {
        return ESP_OK;
    }
    uint64_t start = pv2_now_ms();
    pv2_lock_enter(&scanout->lock, false);
    scanout->closing = true;
    pv2_lock_exit(&scanout->lock, false);
    if (!scanout->source_stopped) {
        esp_err_t error = scanout->config.delete_panel_on_stop ? esp_lcd_panel_del(scanout->config.panel)
                          : scanout->config.stop_scanout(scanout->config.user, timeout);
        if (error != ESP_OK) {
            return error;
        }
        scanout->source_stopped = true;
    }
    for (;;) {
        pv2_lock_enter(&scanout->lock, false);
        bool busy = scanout->active_callbacks != 0;
        if (!busy) {
            scanout->attached = false;
            scanout->stopped = true;
            scanout->cookie = 0;
        }
        pv2_lock_exit(&scanout->lock, false);
        if (!busy) {
            return ESP_OK;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

esp_err_t pv2_lcd_scanout_create(const pv2_lcd_scanout_config_t *config, pv2_lcd_scanout_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->panel || (!config->stop_scanout && !config->delete_panel_on_stop) || !config->width ||
            !config->height || config->width > INT_MAX || config->height > INT_MAX || config->buffer_count < 2 ||
            config->buffer_count > 3 || (config->format != PV2_FORMAT_RGB565 && config->format != PV2_FORMAT_RGB888) ||
            config->kind < PV2_LCD_DSI || config->kind > PV2_LCD_RGB_BOUNCE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->kind == PV2_LCD_RGB_BOUNCE && !config->rgb_bounce_buffer_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    size_t bpp = config->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (config->width > SIZE_MAX / config->height / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_lcd_scanout_t *scanout = pv2_alloc(sizeof(*scanout));
    if (!scanout) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&scanout->lock)) {
        pv2_free(scanout);
        return ESP_ERR_NO_MEM;
    }
    scanout->config = *config;
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PV2_DSI_SUPPORTED
    if (config->kind == PV2_LCD_DSI)
        error = esp_lcd_dpi_panel_get_frame_buffer(config->panel, config->buffer_count, &scanout->buffers[0], &scanout->buffers[1],
                                                   &scanout->buffers[2]);
#endif
#if PV2_RGB_SUPPORTED
    if (config->kind == PV2_LCD_RGB_BOUNCE)
        error = esp_lcd_rgb_panel_get_frame_buffer(config->panel, config->buffer_count, &scanout->buffers[0], &scanout->buffers[1],
                                                   &scanout->buffers[2]);
#endif
    for (size_t i = 0; error == ESP_OK && i < config->buffer_count; ++i) {
        if (!scanout->buffers[i] || (uintptr_t)scanout->buffers[i] % 64) {
            error = ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < i; ++j)
            if (scanout->buffers[i] == scanout->buffers[j]) {
                error = ESP_ERR_INVALID_ARG;
            }
    }
    if (error != ESP_OK) {
        pv2_lock_destroy(&scanout->lock);
        pv2_free(scanout);
        return error;
    }
    *out = scanout;
    return ESP_OK;
}

esp_err_t pv2_lcd_scanout_get_buffers(pv2_lcd_scanout_t *scanout, void **buffers, size_t capacity, size_t *count)
{
    if (!scanout || !count || (capacity && !buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->stopped) {
        return ESP_ERR_INVALID_STATE;
    }
    *count = scanout->config.buffer_count;
    if (capacity < *count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < *count; ++i) {
        buffers[i] = scanout->buffers[i];
    }
    return ESP_OK;
}

esp_err_t pv2_lcd_scanout_get_interface(pv2_lcd_scanout_t *scanout, pv2_panel_adapter_t *out)
{
    if (!scanout || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_panel_adapter_t) {
        scanout, attach, submit, quiesce
    };
    return ESP_OK;
}

esp_err_t pv2_lcd_scanout_destroy(pv2_lcd_scanout_t *scanout)
{
    if (!scanout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->attached || (scanout->closing && !scanout->stopped)) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_lock_destroy(&scanout->lock);
    pv2_free(scanout);
    return ESP_OK;
}

esp_err_t pv2_lcd_scanout_get_output_caps(pv2_lcd_scanout_t *scanout, pv2_output_caps_t *out)
{
    if (!scanout || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_output_caps_t) {
        .x_alignment = 1, .y_alignment = 1, .width_alignment = 1, .height_alignment = 1,
        .base_alignment = 64, .stride_alignment = 1, .tight_stride = true,
        .full_frame_only = true, .external_buffers_required = true, .internal_dma = true, .external_memory = true,
        .max_inflight = 1
    };
    return ESP_OK;
}
