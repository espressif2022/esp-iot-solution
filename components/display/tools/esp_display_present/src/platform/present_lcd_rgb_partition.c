/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform/present_lcd_rgb_partition.h"
#include "platform/present_os.h"
#include "resources/present_memory.h"
#include "esp_lcd_panel_rgb.h"

struct esp_display_present_lcd_rgb_partition {
    present_lock_t lock;
    esp_display_present_lcd_rgb_partition_config_t config;
    esp_display_present_panel_event_fn callback;
    void *user;
    uint64_t cookie;
    size_t active_callbacks;
    bool attached, submitting, closing;
};

static bool PRESENT_ISR_ATTR color_done(esp_lcd_panel_handle_t panel,
                                        const esp_lcd_rgb_panel_event_data_t *data, void *user)
{
    (void)panel;
    (void)data;
    esp_display_present_lcd_rgb_partition_t *rgb = user;
    if (!rgb) return false;
    present_lock_enter(&rgb->lock, true);
    uint64_t cookie = rgb->cookie;
    esp_display_present_panel_event_fn callback = rgb->attached && cookie ? rgb->callback : NULL;
    void *callback_user = rgb->user;
    if (callback) {
        rgb->cookie = 0;
        ++rgb->active_callbacks;
    }
    present_lock_exit(&rgb->lock, true);
    if (!callback) return false;
    esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,
        .transaction_cookie = cookie,
        .result = ESP_OK,
    };
    bool yield = callback(callback_user, &event, true);
    present_lock_enter(&rgb->lock, true);
    --rgb->active_callbacks;
    present_lock_exit(&rgb->lock, true);
    return yield;
}

static esp_err_t rgb_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    esp_display_present_lcd_rgb_partition_t *rgb = context;
    if (!rgb || !callback) return ESP_ERR_INVALID_ARG;
    present_lock_enter(&rgb->lock, false);
    bool busy = rgb->attached || rgb->closing;
    if (!busy) {
        rgb->callback = callback;
        rgb->user = user;
    }
    present_lock_exit(&rgb->lock, false);
    if (busy) return ESP_ERR_INVALID_STATE;
    esp_lcd_rgb_panel_event_callbacks_t callbacks = {.on_color_trans_done = color_done};
    esp_err_t error = esp_lcd_rgb_panel_register_event_callbacks(rgb->config.panel, &callbacks, rgb);
    present_lock_enter(&rgb->lock, false);
    rgb->attached = error == ESP_OK;
    if (error != ESP_OK) {
        rgb->callback = NULL;
        rgb->user = NULL;
    }
    present_lock_exit(&rgb->lock, false);
    return error;
}

static esp_display_present_tx_result_t rgb_submit(void *context, const esp_display_present_surface_t *surface,
                                                   const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_lcd_rgb_partition_t *rgb = context;
    size_t bpp = rgb && rgb->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (!rgb || !surface || !area || !cookie || !area->width || !area->height ||
            area->x > rgb->config.width || area->width > rgb->config.width - area->x ||
            area->y > rgb->config.height || area->height > rgb->config.height - area->y ||
            surface->layout.width != area->width || surface->layout.height != area->height ||
            surface->layout.stride_bytes != area->width * bpp || surface->capacity_bytes < area->width * area->height * bpp ||
            surface->layout.format != rgb->config.format || surface->layout.byte_order != rgb->config.byte_order) {
        return (esp_display_present_tx_result_t){ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG};
    }
    present_lock_enter(&rgb->lock, false);
    bool busy = !rgb->attached || rgb->closing || rgb->submitting || rgb->cookie;
    if (!busy) {
        rgb->cookie = cookie;
        rgb->submitting = true;
    }
    present_lock_exit(&rgb->lock, false);
    if (busy) return (esp_display_present_tx_result_t){ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM};
    esp_err_t error = esp_lcd_panel_draw_bitmap(rgb->config.panel, area->x, area->y,
                                                area->x + area->width, area->y + area->height,
                                                surface->pixels);
    present_lock_enter(&rgb->lock, false);
    rgb->submitting = false;
    if (error != ESP_OK) rgb->closing = true;
    present_lock_exit(&rgb->lock, false);
    return (esp_display_present_tx_result_t){error == ESP_OK ? ESP_DISPLAY_PRESENT_TX_ACCEPTED : ESP_DISPLAY_PRESENT_TX_UNCERTAIN, error};
}

static esp_err_t rgb_quiesce(void *context, uint32_t timeout)
{
    esp_display_present_lcd_rgb_partition_t *rgb = context;
    if (!rgb) return ESP_ERR_INVALID_ARG;
    uint64_t start = present_now_ms();
    for (;;) {
        present_lock_enter(&rgb->lock, false);
        rgb->closing = true;
        bool attached = rgb->attached;
        bool busy = rgb->cookie || rgb->submitting || rgb->active_callbacks;
        present_lock_exit(&rgb->lock, false);
        if (!attached) return ESP_OK;
        if (!busy) break;
        if (present_expired(start, timeout)) return ESP_ERR_TIMEOUT;
        present_pause();
    }
    const esp_lcd_rgb_panel_event_callbacks_t callbacks = {0};
    esp_err_t error = esp_lcd_rgb_panel_register_event_callbacks(rgb->config.panel, &callbacks, NULL);
    if (error != ESP_OK) return error;
    uint64_t elapsed = present_now_ms() - start;
    uint32_t left = timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
    error = present_isr_barrier(left);
    if (error != ESP_OK) return error;
    present_lock_enter(&rgb->lock, false);
    bool busy = rgb->active_callbacks != 0;
    if (busy) {
        present_lock_exit(&rgb->lock, false);
        return ESP_ERR_TIMEOUT;
    }
    rgb->attached = false;
    rgb->callback = NULL;
    rgb->user = NULL;
    present_lock_exit(&rgb->lock, false);
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_rgb_partition_create(const esp_display_present_lcd_rgb_partition_config_t *config,
                                                        esp_display_present_lcd_rgb_partition_t **out)
{
    if (!config || !out || !config->panel || !config->width || !config->height) return ESP_ERR_INVALID_ARG;
    *out = present_alloc(sizeof(**out));
    if (!*out) return ESP_ERR_NO_MEM;
    if (!present_lock_init(&(*out)->lock)) {
        present_free(*out);
        *out = NULL;
        return ESP_ERR_NO_MEM;
    }
    (*out)->config = *config;
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_rgb_partition_get_interface(esp_display_present_lcd_rgb_partition_t *rgb,
                                                               esp_display_present_panel_adapter_t *out)
{
    if (!rgb || !out) return ESP_ERR_INVALID_ARG;
    *out = (esp_display_present_panel_adapter_t){rgb, rgb_attach, rgb_submit, rgb_quiesce};
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_rgb_partition_get_output_caps(esp_display_present_lcd_rgb_partition_t *rgb,
                                                                 esp_display_present_output_caps_t *out)
{
    if (!rgb || !out) return ESP_ERR_INVALID_ARG;
    *out = (esp_display_present_output_caps_t){
        .x_alignment = 1, .y_alignment = 1, .w_align = 1, .h_align = 1,
        .base_alignment = 1, .stride_align = 1, .tight_stride = true,
        .internal_dma = true, .ext_mem = true, .max_inflight = 1,
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_rgb_partition_destroy(esp_display_present_lcd_rgb_partition_t *rgb)
{
    if (!rgb) return ESP_ERR_INVALID_ARG;
    if (rgb->attached) return ESP_ERR_INVALID_STATE;
    present_lock_destroy(&rgb->lock);
    present_free(rgb);
    return ESP_OK;
}
