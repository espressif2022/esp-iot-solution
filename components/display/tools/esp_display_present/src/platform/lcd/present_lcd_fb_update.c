/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform/lcd/present_lcd_fb_update.h"
#include "platform/present_os.h"
#include "resources/present_memory.h"
#include "esp_lcd_panel_rgb.h"
#ifndef PRESENT_HOST
#include "soc/soc_caps.h"
#endif
#if defined(PRESENT_HOST) || SOC_MIPI_DSI_SUPPORTED
#include "esp_lcd_mipi_dsi.h"
#endif

struct esp_display_present_lcd_fb_update {
    present_lock_t lock;
    esp_display_present_lcd_fb_update_config_t config;
    esp_display_present_panel_event_fn callback;
    void *user;
    uint64_t cookie;
    size_t active_callbacks;
    bool attached, submitting, closing;
};

static bool PRESENT_ISR_ATTR blit_done(void *user)
{
    esp_display_present_lcd_fb_update_t *blit = user;
    if (!blit) {
        return false;
    }
    present_lock_enter(&blit->lock, true);
    uint64_t cookie = blit->cookie;
    esp_display_present_panel_event_fn callback = blit->attached && cookie ? blit->callback : NULL;
    void *callback_user = blit->user;
    if (callback) {
        blit->cookie = 0;
        ++blit->active_callbacks;
    }
    present_lock_exit(&blit->lock, true);
    if (!callback) {
        return false;
    }
    esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,
        .transaction_cookie = cookie,
        .result = ESP_OK,
    };
    bool yield = callback(callback_user, &event, true);
    present_lock_enter(&blit->lock, true);
    --blit->active_callbacks;
    present_lock_exit(&blit->lock, true);
    return yield;
}

static bool PRESENT_ISR_ATTR rgb_color_done(esp_lcd_panel_handle_t panel,
                                            const esp_lcd_rgb_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return blit_done(user);
}

#if defined(PRESENT_HOST) || SOC_MIPI_DSI_SUPPORTED
static bool PRESENT_ISR_ATTR dsi_color_done(esp_lcd_panel_handle_t panel,
                                            esp_lcd_dpi_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    return blit_done(user);
}
#endif

static esp_err_t register_color_done(esp_display_present_lcd_fb_update_t *blit, bool attach)
{
    if (blit->config.kind == ESP_DISPLAY_PRESENT_LCD_FB_UPDATE_RGB) {
        esp_lcd_rgb_panel_event_callbacks_t callbacks = {
            .on_color_trans_done = attach ? rgb_color_done : NULL,
        };
        return esp_lcd_rgb_panel_register_event_callbacks(blit->config.panel, &callbacks,
                                                          attach ? blit : NULL);
    }
#if defined(PRESENT_HOST) || SOC_MIPI_DSI_SUPPORTED
    esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = attach ? dsi_color_done : NULL,
    };
    return esp_lcd_dpi_panel_register_event_callbacks(blit->config.panel, &callbacks,
                                                      attach ? blit : NULL);
#else
    (void)attach;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t blit_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    esp_display_present_lcd_fb_update_t *blit = context;
    if (!blit || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&blit->lock, false);
    bool busy = blit->attached || blit->closing;
    if (!busy) {
        blit->callback = callback;
        blit->user = user;
    }
    present_lock_exit(&blit->lock, false);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = register_color_done(blit, true);
    present_lock_enter(&blit->lock, false);
    blit->attached = error == ESP_OK;
    if (error != ESP_OK) {
        blit->callback = NULL;
        blit->user = NULL;
    }
    present_lock_exit(&blit->lock, false);
    return error;
}

static esp_display_present_tx_result_t blit_submit(void *context, const esp_display_present_surface_t *surface,
                                                   const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_lcd_fb_update_t *blit = context;
    size_t bpp = blit && blit->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (!blit || !surface || !area || !cookie || !area->width || !area->height ||
            area->x > blit->config.width || area->width > blit->config.width - area->x ||
            area->y > blit->config.height || area->height > blit->config.height - area->y ||
            surface->layout.width != area->width || surface->layout.height != area->height ||
            surface->layout.stride_bytes != area->width * bpp || surface->capacity_bytes < area->width * area->height * bpp ||
            surface->layout.format != blit->config.format || surface->layout.byte_order != blit->config.byte_order) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
        };
    }
    present_lock_enter(&blit->lock, false);
    bool busy = !blit->attached || blit->closing || blit->submitting || blit->cookie;
    if (!busy) {
        blit->cookie = cookie;
        blit->submitting = true;
    }
    present_lock_exit(&blit->lock, false);
    if (busy) return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM
    };
    esp_err_t error = esp_lcd_panel_draw_bitmap(blit->config.panel, area->x, area->y,
                                                area->x + area->width, area->y + area->height,
                                                surface->pixels);
    present_lock_enter(&blit->lock, false);
    blit->submitting = false;
    if (error != ESP_OK) {
        blit->closing = true;
    }
    present_lock_exit(&blit->lock, false);
    return (esp_display_present_tx_result_t) {
        error == ESP_OK ? ESP_DISPLAY_PRESENT_TX_ACCEPTED : ESP_DISPLAY_PRESENT_TX_UNCERTAIN, error
    };
}

static esp_err_t blit_quiesce(void *context, uint32_t timeout)
{
    esp_display_present_lcd_fb_update_t *blit = context;
    if (!blit) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    for (;;) {
        present_lock_enter(&blit->lock, false);
        blit->closing = true;
        bool attached = blit->attached;
        bool busy = blit->cookie || blit->submitting || blit->active_callbacks;
        present_lock_exit(&blit->lock, false);
        if (!attached) {
            return ESP_OK;
        }
        if (!busy) {
            break;
        }
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
    esp_err_t error = register_color_done(blit, false);
    if (error != ESP_OK) {
        return error;
    }
    uint64_t elapsed = present_now_ms() - start;
    uint32_t left = timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
    error = present_isr_barrier(left);
    if (error != ESP_OK) {
        return error;
    }
    present_lock_enter(&blit->lock, false);
    bool busy = blit->active_callbacks != 0;
    if (busy) {
        present_lock_exit(&blit->lock, false);
        return ESP_ERR_TIMEOUT;
    }
    blit->attached = false;
    blit->callback = NULL;
    blit->user = NULL;
    present_lock_exit(&blit->lock, false);
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_fb_update_create(const esp_display_present_lcd_fb_update_config_t *config,
                                                   esp_display_present_lcd_fb_update_t **out)
{
    if (!config || !out || !config->panel || !config->width || !config->height ||
            config->kind < ESP_DISPLAY_PRESENT_LCD_FB_UPDATE_RGB ||
            config->kind > ESP_DISPLAY_PRESENT_LCD_FB_UPDATE_DSI ||
            (config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB565 &&
             config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB888) ||
            config->byte_order < ESP_DISPLAY_PRESENT_ORDER_NATIVE ||
            config->byte_order > ESP_DISPLAY_PRESENT_ORDER_SWAPPED ||
            (config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB888 &&
             config->byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = present_alloc(sizeof(**out));
    if (!*out) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&(*out)->lock)) {
        present_free(*out);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    (*out)->config = *config;
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_fb_update_get_interface(esp_display_present_lcd_fb_update_t *blit,
                                                          esp_display_present_panel_adapter_t *out)
{
    if (!blit || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_panel_adapter_t) {
        blit, blit_attach, blit_submit, blit_quiesce
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_fb_update_get_output_caps(esp_display_present_lcd_fb_update_t *blit,
                                                            esp_display_present_output_caps_t *out)
{
    if (!blit || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_output_caps_t) {
        .x_alignment = 1, .y_alignment = 1, .w_align = 1, .h_align = 1,
        .base_alignment = 1, .stride_align = 1, .tight_stride = true,
        .internal_dma = true, .ext_mem = true, .max_inflight = 1,
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_fb_update_destroy(esp_display_present_lcd_fb_update_t *blit)
{
    if (!blit) {
        return ESP_ERR_INVALID_ARG;
    }
    if (blit->attached) {
        return ESP_ERR_INVALID_STATE;
    }
    present_lock_destroy(&blit->lock);
    present_free(blit);
    return ESP_OK;
}
