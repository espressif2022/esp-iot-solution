/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lcd_scanout.h"
#include "present_os.h"
#include "esp_cache.h"
#include <limits.h>
#ifdef PRESENT_HOST
#define PRESENT_DSI_SUPPORTED 1
#define PRESENT_RGB_SUPPORTED 1
#else
#include "soc/soc_caps.h"
#define PRESENT_DSI_SUPPORTED SOC_MIPI_DSI_SUPPORTED
#define PRESENT_RGB_SUPPORTED SOC_LCD_RGB_SUPPORTED
#endif
#if PRESENT_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif
#if PRESENT_RGB_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif
struct esp_display_present_lcd_scanout {
    present_lock_t lock;
    esp_display_present_lcd_scanout_config_t config;
    void *buffers[3];
    esp_display_present_panel_event_fn callback;
    void *callback_user;
    uint64_t cookie;
    unsigned boundaries;
    unsigned required_boundaries;
    size_t active_callbacks;
    bool attached, armed, closing, source_stopped, stopped, callbacks_removed;
    bool initial_started, initial_ready, single_vsync;
};
static bool PRESENT_ISR_ATTR frame_boundary(esp_display_present_lcd_scanout_t *scanout)
{
    /* IDF unregister updates callback and user_ctx separately. An ISR that
     * loaded the old function may already observe the cleared context. */
    if (!scanout) {
        return false;
    }
    present_lock_enter(&scanout->lock, true);
    esp_display_present_panel_event_fn callback = NULL;
    void *user = NULL;
    uint64_t cookie = 0;
    unsigned needed = scanout->required_boundaries;
    if (scanout->attached && scanout->armed && (!scanout->closing || scanout->config.retain_driver_scanout) && ++scanout->boundaries == needed) {
        cookie = scanout->cookie;
        scanout->cookie = 0;
        scanout->armed = false;
        callback = scanout->callback;
        user = scanout->callback_user;
        ++scanout->active_callbacks;
    }
    present_lock_exit(&scanout->lock, true);
    if (!callback) {
        return false;
    }
    esp_display_present_backend_event_t event = {.kind = ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED, .transaction_cookie = cookie};
    bool need_yield = callback(user, &event, true);
    present_lock_enter(&scanout->lock, true);
    --scanout->active_callbacks;
    present_lock_exit(&scanout->lock, true);
    return need_yield;
}
static bool PRESENT_ISR_ATTR vsync(esp_display_present_lcd_scanout_t *scanout)
{
    /* IDF unregister updates callback and user_ctx separately. An ISR that
     * loaded the old function may already observe the cleared context. */
    if (!scanout) {
        return false;
    }
    present_lock_enter(&scanout->lock, true);
    esp_display_present_scanout_vsync_fn callback = scanout->attached && !scanout->closing ? scanout->config.on_vsync : NULL;
    if (callback) {
        ++scanout->active_callbacks;
    }
    present_lock_exit(&scanout->lock, true);
    if (!callback) {
        return false;
    }
    bool yield = callback(scanout->config.vsync_user, present_now_us(), true);
    present_lock_enter(&scanout->lock, true);
    --scanout->active_callbacks;
    present_lock_exit(&scanout->lock, true);
    return yield;
}
#if PRESENT_DSI_SUPPORTED
static bool PRESENT_ISR_ATTR dsi_vsync(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return vsync(user);
}
static bool PRESENT_ISR_ATTR dsi_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return frame_boundary(user);
}
#endif
#if PRESENT_RGB_SUPPORTED
static bool PRESENT_ISR_ATTR rgb_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    esp_display_present_lcd_scanout_t *scanout = user;
    bool yield = vsync(scanout);
    /* A sole driver buffer never switches allocations. Explicit active-write
     * mode confirms exposure at VSYNC, without claiming old-FB retirement. */
    if (scanout && scanout->single_vsync) {
        yield |= frame_boundary(scanout);
    }
    return yield;
}
static bool PRESENT_ISR_ATTR rgb_done(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return frame_boundary(user);
}
#endif
static esp_err_t scanout_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    esp_display_present_lcd_scanout_t *scanout = context;
    if (!scanout || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->attached || scanout->closing) {
        return ESP_ERR_INVALID_STATE;
    }
    scanout->callback = callback;
    scanout->callback_user = user;
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PRESENT_DSI_SUPPORTED
    if (scanout->config.kind == ESP_DISPLAY_PRESENT_LCD_DSI) {
        esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = dsi_done, .on_vsync = scanout->config.on_vsync ? dsi_vsync : NULL};
        error = esp_lcd_dpi_panel_register_event_callbacks(scanout->config.panel, &callbacks, scanout);
    }
#endif
#if PRESENT_RGB_SUPPORTED
    if (scanout->config.kind == ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE) {
        esp_lcd_rgb_panel_event_callbacks_t callbacks = {.on_frame_buf_complete = scanout->single_vsync ? NULL : rgb_done, .on_vsync = (scanout->config.on_vsync || scanout->single_vsync) ? rgb_vsync : NULL};
        error = esp_lcd_rgb_panel_register_event_callbacks(scanout->config.panel, &callbacks, scanout);
    }
#endif
    present_lock_enter(&scanout->lock, false);
    if (error == ESP_OK) {
        scanout->attached = true;
    }
    present_lock_exit(&scanout->lock, false);
    return error;
}

static esp_display_present_tx_result_t scanout_submit(void *context, const esp_display_present_surface_t *surface, const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_lcd_scanout_t *scanout = context;
    if (!scanout || !surface || !area || !cookie || area->x || area->y || area->width != scanout->config.width ||
            area->height != scanout->config.height || surface->layout.width != area->width ||
            surface->layout.height != area->height || surface->layout.format != scanout->config.format ||
            surface->layout.byte_order != scanout->config.byte_order)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    size_t bytes = (size_t)scanout->config.width * scanout->config.height * (scanout->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3);
    if (surface->layout.stride_bytes != bytes / scanout->config.height || surface->capacity_bytes < bytes)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_SIZE
    };
    bool known = false;
    for (size_t i = 0; i < scanout->config.buffer_count; ++i) {
        known |= scanout->buffers[i] == surface->pixels;
    }
    if (!known)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    present_lock_enter(&scanout->lock, false);
    bool busy = !scanout->attached || scanout->closing || scanout->cookie;
    present_lock_exit(&scanout->lock, false);
    if (busy)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
    };
    esp_err_t error =
        esp_cache_msync(surface->pixels, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, error
    };
    present_lock_enter(&scanout->lock, false);
    scanout->cookie = cookie;
    scanout->armed = false;
    present_lock_exit(&scanout->lock, false);
    error = esp_lcd_panel_draw_bitmap(scanout->config.panel, 0, 0, area->width, area->height, surface->pixels);
    /* Publish the retirement gate only after a successful driver submission.
     * A callback already executing can have sampled the previous FB before
     * draw_bitmap changed it. Join those ISRs instead of reserving an entire
     * extra frame for them. Callbacks during submit/join see armed == false.
     * With no cross-core join available, retain the two-event fallback. */
    esp_err_t joined = ESP_ERR_NOT_SUPPORTED;
    if (error == ESP_OK && scanout->config.buffer_count > 1) {
        joined = present_isr_barrier(1000);
    }
    present_lock_enter(&scanout->lock, false);
    scanout->boundaries = 0;
    scanout->required_boundaries = (joined == ESP_OK || scanout->config.single_boundary_retire) ? 1u : 2u;
    scanout->armed = error == ESP_OK;
    if (error != ESP_OK) {
        scanout->closing = true;
    }
    present_lock_exit(&scanout->lock, false);
    return (esp_display_present_tx_result_t) {
        error == ESP_OK ? ESP_DISPLAY_PRESENT_TX_ACCEPTED : ESP_DISPLAY_PRESENT_TX_UNCERTAIN, error
    };
}

static esp_err_t detach_retained(esp_display_present_lcd_scanout_t *scanout, uint32_t timeout)
{
    uint64_t start = present_now_ms();
    for (;;) {
        present_lock_enter(&scanout->lock, false);
        bool busy = (scanout->cookie && scanout->armed) || scanout->active_callbacks;
        present_lock_exit(&scanout->lock, false);
        if (!busy) {
            break;
        }
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
    if (!scanout->callbacks_removed) {
        esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PRESENT_DSI_SUPPORTED
        if (scanout->config.kind == ESP_DISPLAY_PRESENT_LCD_DSI) {
            esp_lcd_dpi_panel_event_callbacks_t empty = {0};
            error = esp_lcd_dpi_panel_register_event_callbacks(scanout->config.panel, &empty, NULL);
        }
#endif
#if PRESENT_RGB_SUPPORTED
        if (scanout->config.kind == ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE) {
            esp_lcd_rgb_panel_event_callbacks_t empty = {0};
            error = esp_lcd_rgb_panel_register_event_callbacks(scanout->config.panel, &empty, NULL);
        }
#endif
        if (error != ESP_OK) {
            return error;
        }
        scanout->callbacks_removed = true;
    }
    uint64_t elapsed = present_now_ms() - start;
    uint32_t left = timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
    esp_err_t error = present_isr_barrier(left);
    if (error != ESP_OK) {
        return error;
    }
    present_lock_enter(&scanout->lock, false);
    bool busy = scanout->active_callbacks != 0;
    present_lock_exit(&scanout->lock, false);
    return busy ? ESP_ERR_TIMEOUT : ESP_OK;
}

static esp_err_t scanout_quiesce(void *context, uint32_t timeout)
{
    esp_display_present_lcd_scanout_t *scanout = context;
    if (!scanout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->stopped) {
        return ESP_OK;
    }
    uint64_t start = present_now_ms();
    present_lock_enter(&scanout->lock, false);
    scanout->closing = true;
    present_lock_exit(&scanout->lock, false);
    if (!scanout->source_stopped) {
        esp_err_t error = scanout->config.retain_driver_scanout ? detach_retained(scanout, timeout) :
                          scanout->config.delete_panel_on_stop ? esp_lcd_panel_del(scanout->config.panel) :
                          scanout->config.stop_scanout(scanout->config.user, timeout);
        if (error != ESP_OK) {
            return error;
        }
        scanout->source_stopped = true;
    }
    for (;;) {
        present_lock_enter(&scanout->lock, false);
        bool busy = scanout->active_callbacks != 0;
        if (!busy) {
            scanout->attached = false;
            scanout->stopped = true;
            scanout->cookie = 0;
        }
        present_lock_exit(&scanout->lock, false);
        if (!busy) {
            return ESP_OK;
        }
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
}

esp_err_t esp_display_present_lcd_scanout_create(const esp_display_present_lcd_scanout_config_t *config, esp_display_present_lcd_scanout_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->panel || (!config->stop_scanout && !config->delete_panel_on_stop && !config->retain_driver_scanout) || !config->width ||
            !config->height || config->width > INT_MAX || config->height > INT_MAX || config->buffer_count < 1 ||
            config->buffer_count > 3 || (config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB565 && config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB888) ||
            config->kind < ESP_DISPLAY_PRESENT_LCD_DSI || config->kind > ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->byte_order < ESP_DISPLAY_PRESENT_ORDER_NATIVE || config->byte_order > ESP_DISPLAY_PRESENT_ORDER_SWAPPED ||
            (config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB888 && config->byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->retain_driver_scanout && (config->delete_panel_on_stop || config->stop_scanout)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* A plain frame-complete callback does not identify the FB or submission
     * selected by the driver. One callback cannot prove an old FB retired. */
    if (config->single_boundary_retire && config->buffer_count > 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool single_vsync = false;
#if PRESENT_RGB_SUPPORTED && defined(ESP_LCD_RGB_PANEL_HAS_FRAME_BUFFER_INFO)
    if (config->kind == ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE) {
        esp_lcd_rgb_panel_frame_buffer_info_t info;
        esp_err_t error = esp_lcd_rgb_panel_get_frame_buffer_info(config->panel, &info);
        if (error != ESP_OK) {
            return error;
        }
        size_t bpp = config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
        if (!info.continuous_refresh || info.custom_bounce_callback || info.transformed ||
                info.bits_per_pixel != bpp * 8 || info.frame_buffer_count < config->buffer_count ||
                info.frame_buffer_bytes != (uint64_t)config->width * config->height * bpp) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        single_vsync = config->buffer_count == 1 && info.frame_buffer_count == 1 && !info.frame_buffer_retirement;
        if (!info.frame_buffer_retirement && !single_vsync) {
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
#else
    if (config->kind == ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE && !config->rgb_bounce_buffer_mode) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    size_t bpp = config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (config->width > SIZE_MAX / config->height / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_display_present_lcd_scanout_t *scanout = present_alloc(sizeof(*scanout));
    if (!scanout) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&scanout->lock)) {
        present_free(scanout);
        return ESP_ERR_NO_MEM;
    }
    scanout->config = *config;
    scanout->single_vsync = single_vsync;
    esp_err_t error = ESP_ERR_NOT_SUPPORTED;
#if PRESENT_DSI_SUPPORTED
    if (config->kind == ESP_DISPLAY_PRESENT_LCD_DSI)
        error = esp_lcd_dpi_panel_get_frame_buffer(config->panel, config->buffer_count, &scanout->buffers[0], &scanout->buffers[1],
                                                   &scanout->buffers[2]);
#endif
#if PRESENT_RGB_SUPPORTED
    if (config->kind == ESP_DISPLAY_PRESENT_LCD_RGB_BOUNCE)
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
        present_lock_destroy(&scanout->lock);
        present_free(scanout);
        return error;
    }
    *out = scanout;
    return ESP_OK;
}

/* Compatibility factory calls this before its first producer lease. A new
 * presenter may borrow a panel still scanning a previous presenter's last FB.
 * Establish FB0 explicitly instead of guessing which buffer is initially safe.
 * Timeout retains this adapter/presenter for retry; create never submits pixels. */
esp_err_t present_lcd_scanout_initialize(esp_display_present_lcd_scanout_t *scanout, uint32_t timeout)
{
    if (!scanout || !scanout->attached || scanout->closing) {
        return ESP_ERR_INVALID_STATE;
    }
    if (scanout->initial_ready) {
        return ESP_OK;
    }
    uint64_t start = present_now_ms();
    if (!scanout->initial_started) {
        size_t bytes = (size_t)scanout->config.width * scanout->config.height * (scanout->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3);
        esp_display_present_surface_t surface = {scanout->buffers[0], bytes,
            {
                scanout->config.width, scanout->config.height, bytes / scanout->config.height,
                scanout->config.format, scanout->config.byte_order
            }
        };
        esp_display_present_rect_t area = {0, 0, scanout->config.width, scanout->config.height};
        uint64_t cookie = present_new_token();
        if (!cookie) {
            return ESP_ERR_NO_MEM;
        }
        esp_display_present_tx_result_t tx = scanout_submit(scanout, &surface, &area, cookie);
        if (tx.acceptance != ESP_DISPLAY_PRESENT_TX_REJECTED) {
            scanout->initial_started = true;
        }
        if (tx.result != ESP_OK) {
            return tx.result;
        }
    }
    for (;;) {
        present_lock_enter(&scanout->lock, false);
        bool busy = scanout->cookie || scanout->active_callbacks;
        present_lock_exit(&scanout->lock, false);
        if (!busy) {
            scanout->initial_ready = true;
            return ESP_OK;
        }
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
}

esp_err_t esp_display_present_lcd_scanout_get_buffers(esp_display_present_lcd_scanout_t *scanout, void **buffers, size_t capacity, size_t *count)
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

esp_err_t esp_display_present_lcd_scanout_get_interface(esp_display_present_lcd_scanout_t *scanout, esp_display_present_panel_adapter_t *out)
{
    if (!scanout || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_panel_adapter_t) {
        scanout, scanout_attach, scanout_submit, scanout_quiesce
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_scanout_destroy(esp_display_present_lcd_scanout_t *scanout)
{
    if (!scanout) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scanout->attached || (scanout->closing && !scanout->stopped)) {
        return ESP_ERR_INVALID_STATE;
    }
    present_lock_destroy(&scanout->lock);
    present_free(scanout);
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_scanout_get_output_caps(esp_display_present_lcd_scanout_t *scanout, esp_display_present_output_caps_t *out)
{
    if (!scanout || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_output_caps_t) {
        .x_alignment = 1, .y_alignment = 1, .width_alignment = 1, .height_alignment = 1,
        .base_alignment = 64, .stride_alignment = 1, .tight_stride = true,
        .full_frame_only = true, .external_buffers_required = true, .internal_dma = true, .external_memory = true,
        .max_inflight = 1
    };
    return ESP_OK;
}

esp_err_t present_lcd_scanout_set_layout(esp_display_present_lcd_scanout_t *scanout, uint32_t width, uint32_t height)
{
    if (!scanout || !width || !height || width > INT_MAX || height > INT_MAX ||
            (uint64_t)width * height != (uint64_t)scanout->config.width * scanout->config.height) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&scanout->lock, false);
    bool busy = scanout->closing || scanout->cookie;
    if (!busy) {
        scanout->config.width = width;
        scanout->config.height = height;
    }
    present_lock_exit(&scanout->lock, false);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}
