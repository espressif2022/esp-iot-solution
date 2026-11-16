/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform/lcd/present_lcd_io.h"
#include "platform/present_os.h"
#include "resources/present_memory.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include <limits.h>
#include <string.h>
#ifdef PRESENT_HOST
#define PRESENT_SPI_EXTERNAL_DMA 1
#else
#include "soc/soc_caps.h"
#define PRESENT_SPI_EXTERNAL_DMA SOC_PSRAM_DMA_CAPABLE
#endif

typedef enum {
    STAGE_FREE, STAGE_FILLING, STAGE_READY, STAGE_SUBMITTED
} stage_state_t;

typedef struct {
    void *pixels;
    uint32_t row, rows;
    stage_state_t state;
    bool returned, done;
} stage_slot_t;

struct esp_display_present_lcd_io {
    present_lock_t lock;
    esp_display_present_lcd_io_config_t config;
    esp_display_present_panel_event_fn callback;
    void *user;
    uint64_t cookies[16];
    size_t head, count, active_callbacks;
    bool attached, closing, submitting;
    struct {
        stage_slot_t slots[2];
        present_os_event_t *event;
        int active; /* At most one draw_bitmap/color transfer is in flight. */
        bool abort, started, result_finalized;
        esp_err_t result;
    } stage;
};

/* Caller holds lock. A returned error alone never proves a submitted band retired. */
static bool PRESENT_ISR_ATTR stages_retired(const esp_display_present_lcd_io_t *spi)
{
    for (unsigned i = 0; i < 2; ++i) {
        const stage_slot_t *slot = &spi->stage.slots[i];
        if (slot->state == STAGE_FILLING ||
                (slot->state == STAGE_SUBMITTED && (!slot->returned || !slot->done))) {
            return false;
        }
    }
    return true;
}

static bool PRESENT_ISR_ATTR publish_color(esp_display_present_lcd_io_t *spi, bool isr)
{
    present_lock_enter(&spi->lock, isr);
    if (!spi->attached || !spi->count ||
            (spi->config.staging_bytes && (spi->submitting || !stages_retired(spi)))) {
        present_lock_exit(&spi->lock, isr);
        return false;
    }
    uint64_t cookie = spi->cookies[spi->head];
    spi->head = (spi->head + 1) % spi->config.max_inflight;
    --spi->count;
    ++spi->active_callbacks;
    esp_display_present_panel_event_fn callback = spi->callback;
    void *context = spi->user;
    esp_err_t result = spi->stage.result;
    present_lock_exit(&spi->lock, isr);
    const esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,
        .transaction_cookie = cookie,
        .result = result,
    };
    bool yield = callback(context, &event, isr);
    present_lock_enter(&spi->lock, isr);
    --spi->active_callbacks;
    present_lock_exit(&spi->lock, isr);
    return yield;
}

/* Exclusive IO, FIFO and one tx_color per draw_bitmap remain transport contracts.
 * A driver callback retires one band, not the logical frame. No ISR copy/submit. */
static bool PRESENT_ISR_ATTR finish_color(esp_display_present_lcd_io_t *spi, bool isr)
{
    if (!spi->config.staging_bytes) {
        return publish_color(spi, isr);
    }
    present_lock_enter(&spi->lock, isr);
    if (!spi->attached || !spi->count || spi->stage.active < 0) {
        present_lock_exit(&spi->lock, isr);
        return false;
    }
    ++spi->active_callbacks;
    spi->stage.slots[spi->stage.active].done = true;
    present_lock_exit(&spi->lock, isr);
    bool yield = present_event_signal(spi->stage.event, isr);
    /* Also retires a late callback after a timed-out submit has exited. */
    yield |= publish_color(spi, isr);
    present_lock_enter(&spi->lock, isr);
    --spi->active_callbacks;
    present_lock_exit(&spi->lock, isr);
    return yield;
}

static bool PRESENT_ISR_ATTR color_done(esp_lcd_panel_io_handle_t io,
                                        esp_lcd_panel_io_event_data_t *data, void *user)
{
    (void)io;
    (void)data;
    return user ? finish_color(user, true) : false;
}

static esp_err_t spi_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!spi || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&spi->lock, false);
    if (spi->attached || spi->closing) {
        present_lock_exit(&spi->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    spi->callback = callback;
    spi->user = user;
    spi->attached = true;
    present_lock_exit(&spi->lock, false);
    const esp_lcd_panel_io_callbacks_t callbacks = { .on_color_trans_done = color_done };
    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(spi->config.io, &callbacks, spi);
    if (ret != ESP_OK) {
        present_lock_enter(&spi->lock, false);
        spi->attached = false;
        spi->callback = NULL;
        spi->user = NULL;
        present_lock_exit(&spi->lock, false);
    }
    return ret;
}

static esp_err_t validate_tx(esp_display_present_lcd_io_t *spi, const esp_display_present_surface_t *surface,
                             const esp_display_present_rect_t *area, uint64_t cookie, size_t *bytes)
{
    if (!surface || !area || !surface->pixels || !cookie || !area->width || !area->height ||
            surface->layout.width != area->width || surface->layout.height != area->height ||
            surface->layout.format != spi->config.format || surface->layout.byte_order != spi->config.byte_order ||
            area->x > INT_MAX || area->y > INT_MAX ||
            area->width > (uint32_t)INT_MAX - area->x || area->height > (uint32_t)INT_MAX - area->y) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = spi->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (area->width > SIZE_MAX / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t row = area->width * bpp;
    if (surface->layout.stride_bytes != row || area->height > SIZE_MAX / row) {
        return ESP_ERR_INVALID_SIZE;
    }
    *bytes = row * area->height;
    if (*bytes > surface->capacity_bytes || (uintptr_t)surface->pixels % PRESENT_LCD_IO_BASE_ALIGNMENT ||
            (uintptr_t)surface->pixels > UINTPTR_MAX - *bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (spi->config.staging_bytes) {
        return row <= spi->config.staging_bytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }
    /* Check both ends: a valid base alone does not establish the entire range. */
    const uint8_t *last = (const uint8_t *)surface->pixels + *bytes - 1;
    bool valid = esp_ptr_internal(surface->pixels) && esp_ptr_internal(last) &&
                 esp_ptr_dma_capable(surface->pixels) && esp_ptr_dma_capable(last);
#if PRESENT_SPI_EXTERNAL_DMA
    if (!valid && spi->config.psram_dma_direct && *bytes % PRESENT_LCD_IO_EXTERNAL_LENGTH_ALIGNMENT == 0) {
        valid = esp_ptr_dma_ext_capable(surface->pixels) && esp_ptr_dma_ext_capable(last);
    }
#endif
    if (!valid) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t stage_fill(esp_display_present_lcd_io_t *spi, unsigned index,
                            const esp_display_present_surface_t *surface, uint32_t row, uint32_t rows)
{
    present_lock_enter(&spi->lock, false);
    if (spi->stage.abort) {
        esp_err_t result = spi->stage.result;
        present_lock_exit(&spi->lock, false);
        return result;
    }
    stage_slot_t *slot = &spi->stage.slots[index];
    slot->state = STAGE_FILLING;
    slot->row = row;
    slot->rows = rows;
    present_lock_exit(&spi->lock, false);
    size_t bytes = (size_t)rows * surface->layout.stride_bytes;
    memcpy(slot->pixels, (const uint8_t *)surface->pixels + (size_t)row * surface->layout.stride_bytes, bytes);
    esp_err_t result = esp_cache_msync(slot->pixels, bytes,
                                       ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    present_lock_enter(&spi->lock, false);
    slot->state = STAGE_READY;
    if (result == ESP_OK || result == ESP_ERR_NOT_SUPPORTED) {
        result = spi->stage.abort ? spi->stage.result : ESP_OK;
    }
    present_lock_exit(&spi->lock, false);
    return result;
}

static void spi_request_abort(void *context)
{
    esp_display_present_lcd_io_t *spi = context;
    present_lock_enter(&spi->lock, false);
    spi->closing = true;
    spi->stage.abort = true;
    if (spi->config.staging_bytes && !spi->stage.result_finalized && spi->stage.result == ESP_OK) {
        spi->stage.result = ESP_ERR_INVALID_STATE;
    }
    present_lock_exit(&spi->lock, false);
    if (spi->stage.event) {
        (void)present_event_signal(spi->stage.event, false);
    }
}

/* One frame, two fixed scratch slots, one DMA band in flight. Fill the other
 * slot while the current band sends. Wire lengths always match real pixels. */
static esp_err_t stage_transfer(esp_display_present_lcd_io_t *spi, const esp_display_present_surface_t *surface,
                                const esp_display_present_rect_t *area)
{
    uint32_t band_rows = spi->config.staging_bytes / surface->layout.stride_bytes;
    uint64_t start = present_now_ms();
    uint32_t timeout = spi->config.staging_timeout_ms ? spi->config.staging_timeout_ms : 1000;
    uint32_t first = area->height < band_rows ? area->height : band_rows;
    esp_err_t result = stage_fill(spi, 0, surface, 0, first);
    unsigned current = 0;
    uint32_t next_row = first;
    while (result == ESP_OK) {
        if (present_expired(start, timeout)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        stage_slot_t *slot = &spi->stage.slots[current];
        present_lock_enter(&spi->lock, false);
        if (spi->stage.abort) {
            result = spi->stage.result;
            present_lock_exit(&spi->lock, false);
            break;
        }
        spi->stage.started = true;
        slot->state = STAGE_SUBMITTED;
        slot->done = slot->returned = false;
        spi->stage.active = current;
        present_lock_exit(&spi->lock, false);
        result = esp_lcd_panel_draw_bitmap(spi->config.panel, area->x, area->y + slot->row,
                                           area->x + area->width, area->y + slot->row + slot->rows, slot->pixels);
        present_lock_enter(&spi->lock, false);
        slot->returned = true;
        present_lock_exit(&spi->lock, false);
        if (result != ESP_OK) {
            break; /* May have queued chunks without a final callback. Keep slot pinned. */
        }
        if (present_expired(start, timeout)) {
            result = ESP_ERR_TIMEOUT;
            break; /* Even an early callback cannot excuse a late driver return. */
        }
        if (next_row < area->height) {
            uint32_t n = area->height - next_row;
            if (n > band_rows) {
                n = band_rows;
            }
            result = stage_fill(spi, current ^ 1, surface, next_row, n);
            next_row += n;
            if (result != ESP_OK) {
                break;
            }
        }
        for (;;) {
            present_lock_enter(&spi->lock, false);
            bool done = slot->done;
            bool abort = spi->stage.abort;
            if (abort) {
                result = spi->stage.result;
            }
            present_lock_exit(&spi->lock, false);
            if (done || abort) {
                break;
            }
            uint64_t elapsed = present_now_ms() - start;
            uint32_t remaining = timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
            if (!remaining || !present_event_wait(spi->stage.event, remaining)) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
        }
        if (result == ESP_OK && present_expired(start, timeout)) {
            result = ESP_ERR_TIMEOUT;
        }
        if (result != ESP_OK) {
            break;
        }
        present_lock_enter(&spi->lock, false);
        slot->state = STAGE_FREE;
        spi->stage.active = -1;
        bool abort = spi->stage.abort;
        if (abort) {
            result = spi->stage.result;
        }
        present_lock_exit(&spi->lock, false);
        if (abort || slot->row + slot->rows == area->height) {
            break;
        }
        current ^= 1;
    }
    return result;
}

static esp_display_present_tx_result_t spi_submit(void *context, const esp_display_present_surface_t *surface,
                                                  const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!spi) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
        };
    }
    size_t bytes;
    esp_err_t ret = validate_tx(spi, surface, area, cookie, &bytes);
    if (ret != ESP_OK) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ret
        };
    }
    present_lock_enter(&spi->lock, false);
    if (!spi->attached || spi->closing || spi->submitting) {
        present_lock_exit(&spi->lock, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    if (spi->count == spi->config.max_inflight) {
        present_lock_exit(&spi->lock, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM
        };
    }
    present_lock_exit(&spi->lock, false);

    ret = spi->config.staging_bytes ? ESP_OK : esp_cache_msync(surface->pixels, bytes,
                                                               ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    /* esp_cache_msync documents NOT_SUPPORTED for a non-cacheable range. */
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ret
        };
    }
    present_lock_enter(&spi->lock, false);
    /* stop can race the cache operation above; never enqueue after detach/abort. */
    if (!spi->attached || spi->closing || spi->submitting || spi->count == spi->config.max_inflight) {
        present_lock_exit(&spi->lock, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    spi->stage.result = ESP_OK;
    spi->stage.started = spi->stage.result_finalized = false;
    spi->cookies[(spi->head + spi->count) % spi->config.max_inflight] = cookie;
    ++spi->count;
    spi->submitting = true;
    present_lock_exit(&spi->lock, false);

    if (spi->config.staging_bytes) {
        ret = stage_transfer(spi, surface, area);
    } else {
        ret = esp_lcd_panel_draw_bitmap(spi->config.panel, (int)area->x, (int)area->y,
                                        (int)(area->x + area->width), (int)(area->y + area->height),
                                        surface->pixels);
    }
    present_lock_enter(&spi->lock, false);
    /* Commit the transaction outcome with submitting, before publishing. A
     * later stop must not retroactively turn a completed success into failure. */
    if (spi->config.staging_bytes) {
        spi->stage.result = ret;
        spi->stage.result_finalized = true;
        if (ret != ESP_OK && !spi->stage.started) {
            /* No draw was reserved: drop our sole cookie without a completion.
             * Source access ended, so cache/preflight errors are retryable. */
            --spi->count;
            spi->submitting = false;
            present_lock_exit(&spi->lock, false);
            return (esp_display_present_tx_result_t) {
                ESP_DISPLAY_PRESENT_TX_REJECTED, ret
            };
        }
        /* A late ISR may publish first. Keep quiesce from detaching/freeing
         * this adapter before the task's own publication attempt has exited. */
        ++spi->active_callbacks;
    }
    spi->submitting = false;
    if (ret != ESP_OK) {
        spi->closing = true;
    }
    present_lock_exit(&spi->lock, false);
    if (spi->config.staging_bytes) {
        (void)publish_color(spi, false);
        present_lock_enter(&spi->lock, false);
        --spi->active_callbacks;
        present_lock_exit(&spi->lock, false);
    }
    /* A failed split transfer can still own the DMA source. Never pop it. */
    return (esp_display_present_tx_result_t) {
        ret == ESP_OK ? ESP_DISPLAY_PRESENT_TX_ACCEPTED : ESP_DISPLAY_PRESENT_TX_UNCERTAIN, ret
    };
}

static esp_err_t spi_quiesce(void *context, uint32_t timeout)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!spi) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_request_abort(spi);
    uint64_t start = present_now_ms();
    for (;;) {
        present_lock_enter(&spi->lock, false);
        bool attached = spi->attached;
        bool busy = spi->count || spi->active_callbacks || spi->submitting;
        present_lock_exit(&spi->lock, false);
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
    /* Exclusive SPI IO + all color-done callbacks returned means no later
     * color event can begin. Registering an empty callback does not drain DMA. */
    const esp_lcd_panel_io_callbacks_t callbacks = {0};
    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(spi->config.io, &callbacks, NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    present_lock_enter(&spi->lock, false);
    spi->attached = false;
    spi->callback = NULL;
    spi->user = NULL;
    present_lock_exit(&spi->lock, false);
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_io_create(const esp_display_present_lcd_io_config_t *config, esp_display_present_lcd_io_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->io || !config->panel || !config->max_inflight || config->max_inflight > 16 ||
            (config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB565 && config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB888) ||
            (config->byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE && config->byte_order != ESP_DISPLAY_PRESENT_ORDER_SWAPPED) ||
            (config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB888 && config->byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
#if !PRESENT_SPI_EXTERNAL_DMA
    if (config->psram_dma_direct) {
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    esp_display_present_lcd_io_t *spi = present_alloc(sizeof(*spi));
    if (!spi) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&spi->lock)) {
        present_free(spi);
        return ESP_ERR_NO_MEM;
    }
    spi->config = *config;
    spi->stage.active = -1;
    if (config->staging_bytes) {
        spi->config.max_inflight = 1;
        spi->stage.slots[0].pixels = present_alloc_pixels(config->staging_bytes);
        spi->stage.slots[1].pixels = present_alloc_pixels(config->staging_bytes);
        spi->stage.event = present_event_create();
        if (!spi->stage.slots[0].pixels || !spi->stage.slots[1].pixels || !spi->stage.event) {
            present_free(spi->stage.slots[0].pixels);
            present_free(spi->stage.slots[1].pixels);
            present_event_destroy(spi->stage.event);
            present_lock_destroy(&spi->lock);
            present_free(spi);
            return ESP_ERR_NO_MEM;
        }
    }
    *out = spi;
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_io_get_interface(esp_display_present_lcd_io_t *spi, esp_display_present_panel_adapter_t *out)
{
    if (!spi || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_panel_adapter_t) {
        spi, spi_attach, spi_submit, spi_quiesce, spi_request_abort
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_io_destroy(esp_display_present_lcd_io_t *spi)
{
    if (!spi) {
        return ESP_ERR_INVALID_ARG;
    }
    if (spi->attached || spi->count || spi->active_callbacks || spi->submitting) {
        return ESP_ERR_INVALID_STATE;
    }
    present_event_destroy(spi->stage.event);
    present_free(spi->stage.slots[0].pixels);
    present_free(spi->stage.slots[1].pixels);
    present_lock_destroy(&spi->lock);
    present_free(spi);
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_io_get_output_caps(esp_display_present_lcd_io_t *spi, esp_display_present_output_caps_t *out)
{
    if (!spi || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    bool direct_external_dma = spi->config.psram_dma_direct && !spi->config.staging_bytes;
    *out = (esp_display_present_output_caps_t) {
        .x_alignment = 1,
        .y_alignment = 1,
        .w_align = 1,
        .h_align = 1,
        .base_alignment = PRESENT_LCD_IO_BASE_ALIGNMENT,
        .stride_align = 1,
        .tight_stride = true,
        .full_only = false,
        .internal_dma = true,
        .ext_mem = spi->config.psram_dma_direct || spi->config.staging_bytes != 0,
        .max_inflight = spi->config.max_inflight,
        .ext_len_align = direct_external_dma ? PRESENT_LCD_IO_EXTERNAL_LENGTH_ALIGNMENT : 1,
    };
    return ESP_OK;
}

size_t esp_display_present_lcd_io_memory(size_t staging_bytes)
{
    if (staging_bytes > (SIZE_MAX - sizeof(esp_display_present_lcd_io_t) - present_event_memory()) / 2 - PRESENT_LCD_IO_BASE_ALIGNMENT) {
        return SIZE_MAX;
    }
    size_t aligned = (staging_bytes + PRESENT_LCD_IO_BASE_ALIGNMENT - 1) & ~(size_t)(PRESENT_LCD_IO_BASE_ALIGNMENT - 1);
    return sizeof(esp_display_present_lcd_io_t) + (staging_bytes ? 2 * aligned + present_event_memory() : 0);
}
