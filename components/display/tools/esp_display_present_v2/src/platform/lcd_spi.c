/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lcd_spi.h"
#include "present_os.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include <limits.h>
#ifdef PV2_HOST
#define PV2_SPI_EXTERNAL_DMA 1
#else
#include "soc/soc_caps.h"
#define PV2_SPI_EXTERNAL_DMA SOC_PSRAM_DMA_CAPABLE
#endif

struct pv2_lcd_spi {
    pv2_lock_t lock;
    pv2_lcd_spi_config_t config;
    pv2_panel_event_fn callback;
    void *user;
    uint64_t cookies[16];
    size_t head, count, active_callbacks;
    bool attached, closing, submitting;
};

static bool PV2_ISR_ATTR color_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *data, void *user)
{
    (void)io;
    (void)data;
    pv2_lcd_spi_t *a = user;
    pv2_lock_enter(&a->lock, true);
    if (!a->attached || !a->count) {
        pv2_lock_exit(&a->lock, true);
        return false;
    }
    uint64_t cookie = a->cookies[a->head];
    a->head = (a->head + 1) % a->config.max_inflight;
    --a->count;
    ++a->active_callbacks;
    pv2_panel_event_fn callback = a->callback;
    void *context = a->user;
    pv2_lock_exit(&a->lock, true);

    const pv2_backend_event_t event = {
        .kind = PV2_EVENT_TRANSFER_DONE, .transaction_cookie = cookie, .result = ESP_OK,
    };
    bool need_yield = callback(context, &event, true);

    pv2_lock_enter(&a->lock, true);
    --a->active_callbacks;
    pv2_lock_exit(&a->lock, true);
    return need_yield;
}

static esp_err_t attach(void *context, pv2_panel_event_fn callback, void *user)
{
    pv2_lcd_spi_t *a = context;
    if (!a || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&a->lock, false);
    if (a->attached || a->closing) {
        pv2_lock_exit(&a->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    a->callback = callback;
    a->user = user;
    a->attached = true;
    pv2_lock_exit(&a->lock, false);
    const esp_lcd_panel_io_callbacks_t callbacks = { .on_color_trans_done = color_done };
    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(a->config.io, &callbacks, a);
    if (ret != ESP_OK) {
        pv2_lock_enter(&a->lock, false);
        a->attached = false;
        a->callback = NULL;
        a->user = NULL;
        pv2_lock_exit(&a->lock, false);
    }
    return ret;
}

static esp_err_t validate_tx(pv2_lcd_spi_t *a, const pv2_surface_t *s,
                             const pv2_rect_t *area, uint64_t cookie, size_t *bytes)
{
    if (!s || !area || !s->pixels || !cookie || !area->width || !area->height ||
            s->layout.width != area->width || s->layout.height != area->height ||
            s->layout.format != a->config.format || s->layout.byte_order != a->config.byte_order ||
            area->x > INT_MAX || area->y > INT_MAX ||
            area->width > (uint32_t)INT_MAX - area->x || area->height > (uint32_t)INT_MAX - area->y) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = a->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (area->width > SIZE_MAX / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t row = area->width * bpp;
    if (s->layout.stride_bytes != row || area->height > SIZE_MAX / row) {
        return ESP_ERR_INVALID_SIZE;
    }
    *bytes = row * area->height;
    if (*bytes > s->capacity_bytes || (uintptr_t)s->pixels % 64 ||
            (uintptr_t)s->pixels > UINTPTR_MAX - *bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    /* Check both ends: a valid base alone does not establish the entire range. */
    const uint8_t *last = (const uint8_t *)s->pixels + *bytes - 1;
    bool valid = esp_ptr_internal(s->pixels) && esp_ptr_internal(last) &&
                 esp_ptr_dma_capable(s->pixels) && esp_ptr_dma_capable(last);
#if PV2_SPI_EXTERNAL_DMA
    if (!valid && a->config.psram_dma_direct && *bytes % 64 == 0) {
        valid = esp_ptr_dma_ext_capable(s->pixels) && esp_ptr_dma_ext_capable(last);
    }
#endif
    if (!valid) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static pv2_tx_result_t submit(void *context, const pv2_surface_t *s,
                              const pv2_rect_t *area, uint64_t cookie)
{
    pv2_lcd_spi_t *a = context;
    if (!a) {
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ESP_ERR_INVALID_ARG
        };
    }
    size_t bytes;
    esp_err_t ret = validate_tx(a, s, area, cookie, &bytes);
    if (ret != ESP_OK) {
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ret
        };
    }
    pv2_lock_enter(&a->lock, false);
    if (!a->attached || a->closing || a->submitting) {
        pv2_lock_exit(&a->lock, false);
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    if (a->count == a->config.max_inflight) {
        pv2_lock_exit(&a->lock, false);
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ESP_ERR_NO_MEM
        };
    }
    pv2_lock_exit(&a->lock, false);

    ret = esp_cache_msync(s->pixels, bytes,
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    /* esp_cache_msync documents NOT_SUPPORTED for a non-cacheable range. */
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ret
        };
    }
    pv2_lock_enter(&a->lock, false);
    a->cookies[(a->head + a->count) % a->config.max_inflight] = cookie;
    ++a->count;
    a->submitting = true;
    pv2_lock_exit(&a->lock, false);

    ret = esp_lcd_panel_draw_bitmap(a->config.panel, (int)area->x, (int)area->y,
                                    (int)(area->x + area->width), (int)(area->y + area->height),
                                    s->pixels);
    pv2_lock_enter(&a->lock, false);
    a->submitting = false;
    if (ret != ESP_OK) {
        a->closing = true;
    }
    pv2_lock_exit(&a->lock, false);
    /* A failed split transfer can still own the DMA source. Never pop it. */
    return (pv2_tx_result_t) {
        ret == ESP_OK ? PV2_TX_ACCEPTED : PV2_TX_UNCERTAIN, ret
    };
}

static esp_err_t quiesce(void *context, uint32_t timeout)
{
    pv2_lcd_spi_t *a = context;
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        pv2_lock_enter(&a->lock, false);
        a->closing = true;
        bool attached = a->attached;
        bool busy = a->count || a->active_callbacks || a->submitting;
        pv2_lock_exit(&a->lock, false);
        if (!attached) {
            return ESP_OK;
        }
        if (!busy) {
            break;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
    /* Exclusive SPI IO + all color-done callbacks returned means no later
     * color event can begin. Registering an empty callback does not drain DMA. */
    const esp_lcd_panel_io_callbacks_t callbacks = {0};
    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(a->config.io, &callbacks, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    pv2_lock_enter(&a->lock, false);
    a->attached = false;
    a->callback = NULL;
    a->user = NULL;
    pv2_lock_exit(&a->lock, false);
    return ESP_OK;
}

esp_err_t pv2_lcd_spi_create(const pv2_lcd_spi_config_t *config, pv2_lcd_spi_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->io || !config->panel || !config->max_inflight || config->max_inflight > 16 ||
            (config->format != PV2_FORMAT_RGB565 && config->format != PV2_FORMAT_RGB888) ||
            (config->byte_order != PV2_ORDER_NATIVE && config->byte_order != PV2_ORDER_SWAPPED) ||
            (config->format == PV2_FORMAT_RGB888 && config->byte_order != PV2_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
#if !PV2_SPI_EXTERNAL_DMA
    if (config->psram_dma_direct) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    pv2_lcd_spi_t *a = pv2_alloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&a->lock)) {
        pv2_free(a);
        return ESP_ERR_NO_MEM;
    }
    a->config = *config;
    *out = a;
    return ESP_OK;
}

esp_err_t pv2_lcd_spi_get_interface(pv2_lcd_spi_t *a, pv2_panel_adapter_t *out)
{
    if (!a || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_panel_adapter_t) {
        a, attach, submit, quiesce
    };
    return ESP_OK;
}

esp_err_t pv2_lcd_spi_destroy(pv2_lcd_spi_t *a)
{
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    if (a->attached || a->count || a->active_callbacks || a->submitting) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_lock_destroy(&a->lock);
    pv2_free(a);
    return ESP_OK;
}

esp_err_t pv2_lcd_spi_get_output_caps(pv2_lcd_spi_t *a, pv2_output_caps_t *out)
{
    if (!a || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_output_caps_t) {
        .x_alignment = 1, .y_alignment = 1, .width_alignment = 1, .height_alignment = 1,
        .base_alignment = 64, .stride_alignment = 1, .tight_stride = true,
        .full_frame_only = false, .internal_dma = true, .external_memory = a->config.psram_dma_direct,
        .max_inflight = a->config.max_inflight, .external_length_alignment = a->config.psram_dma_direct ? 64 : 1
    };
    return ESP_OK;
}
