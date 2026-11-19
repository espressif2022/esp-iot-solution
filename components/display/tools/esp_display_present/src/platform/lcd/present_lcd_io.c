/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform/lcd/present_lcd_io.h"
#include "platform/present_os.h"
#include "resources/present_memory.h"
#include "pixel/present_pixel_ops_internal.h"
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

enum {
    PRESENT_SPI_BASE_ALIGNMENT = 64,
    PRESENT_SPI_EXTERNAL_LENGTH_ALIGNMENT = 64,
};

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
    size_t head, count, active_callbacks, active_calls;
    bool attached, closing, submitting;
    struct {
        stage_slot_t slots[2];
        present_os_event_t *event;
        int active; /* At most one draw_bitmap/color transfer is in flight. */
        bool abort, started, result_finalized, staged;
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
            (spi->stage.staged && (spi->submitting || !stages_retired(spi)))) {
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
    present_lock_enter(&spi->lock, isr);
    if (!spi->stage.staged) {
        present_lock_exit(&spi->lock, isr);
        return publish_color(spi, isr);
    }
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
                             const esp_display_present_rect_t *area, uint64_t cookie, size_t *bytes, bool staged)
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
    if (*bytes > surface->capacity_bytes || (!staged && (uintptr_t)surface->pixels % PRESENT_SPI_BASE_ALIGNMENT) ||
            (uintptr_t)surface->pixels > UINTPTR_MAX - *bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (staged) {
        return row <= spi->config.staging_bytes ? ESP_OK : ESP_ERR_INVALID_SIZE;
    }
    /* Check both ends: a valid base alone does not establish the entire range. */
    const uint8_t *last = (const uint8_t *)surface->pixels + *bytes - 1;
    bool valid = esp_ptr_internal(surface->pixels) && esp_ptr_internal(last) &&
                 esp_ptr_dma_capable(surface->pixels) && esp_ptr_dma_capable(last);
#if PRESENT_SPI_EXTERNAL_DMA
    /* REGION delegates transaction-length alignment to IDF SPI, as native
     * panel IO clients do. Its DMA/cache constraints and optional temporary
     * buffer are checked per driver chunk. Keep the raw-submit contract. */
    if (!valid && spi->config.psram_dma_direct &&
            (spi->config.region_mode || *bytes % PRESENT_SPI_EXTERNAL_LENGTH_ALIGNMENT == 0)) {
        valid = esp_ptr_dma_ext_capable(surface->pixels) && esp_ptr_dma_ext_capable(last);
    }
#endif
    if (!valid) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t stage_fill(esp_display_present_lcd_io_t *spi, unsigned index,
                            const esp_display_present_surface_t *surface, uint32_t row, uint32_t rows,
                            const esp_display_present_surface_t *source, esp_display_present_engine_rotation_t rotation)
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
    if (!source) {
        memcpy(slot->pixels, (const uint8_t *)surface->pixels + (size_t)row * surface->layout.stride_bytes, bytes);
    } else if (rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0) {
        /* Reuse the packed/strided CPU fast path: RGB565 copy+swap processes
         * aligned pixel pairs, with byte-safe fallback for odd source strides.
         * The source stays borrowed and unchanged until logical completion. */
        const esp_display_present_copy_request_t copy = {
            .src = {source->pixels, source->capacity_bytes, source->layout},
            .dst = {
                slot->pixels, bytes, {
                    surface->layout.width, rows, surface->layout.stride_bytes,
                    surface->layout.format, surface->layout.byte_order
                }
            },
            .src_area = {0, row, surface->layout.width, rows},
            .rotation = ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0,
        };
        present_pixel_copy_cpu_validated(&copy);
    } else {
        /* Generate physical packed rows directly; no full-size conversion pool.
         * CPU is the fallback for arbitrary strides/quarter-turn tail windows. */
        size_t bpp = source->layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
        bool swap = source->layout.byte_order != spi->config.byte_order;
        uint8_t *destination = slot->pixels;
        for (uint32_t y = row; y < row + rows; ++y) {
            for (uint32_t x = 0; x < surface->layout.width; ++x) {
                uint32_t sx, sy;
                switch (rotation) {
                case ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90:
                    sx = y; sy = source->layout.height - 1 - x; break;
                case ESP_DISPLAY_PRESENT_ENGINE_ROTATE_180:
                    sx = source->layout.width - 1 - x; sy = source->layout.height - 1 - y; break;
                case ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270:
                    sx = source->layout.width - 1 - y; sy = x; break;
                default:
                    sx = x; sy = y; break;
                }
                const uint8_t *pixel = (const uint8_t *)source->pixels +
                                       (size_t)sy * source->layout.stride_bytes + (size_t)sx * bpp;
                if (swap) {
                    *destination++ = pixel[1];
                    *destination++ = pixel[0];
                } else {
                    memcpy(destination, pixel, bpp);
                    destination += bpp;
                }
            }
        }
    }
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
                                const esp_display_present_rect_t *area,
                                const esp_display_present_surface_t *source, esp_display_present_engine_rotation_t rotation)
{
    uint32_t band_rows = spi->config.staging_bytes / surface->layout.stride_bytes;
    uint64_t start = present_now_ms();
    uint32_t timeout = spi->config.staging_timeout_ms ? spi->config.staging_timeout_ms : 1000;
    uint32_t first = area->height < band_rows ? area->height : band_rows;
    esp_err_t result = stage_fill(spi, 0, surface, 0, first, source, rotation);
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
            result = stage_fill(spi, current ^ 1, surface, next_row, n, source, rotation);
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

static esp_display_present_tx_result_t submit_pixels(void *context, const esp_display_present_surface_t *surface,
                                                     const esp_display_present_rect_t *area, uint64_t cookie, bool staged,
                                                     const esp_display_present_surface_t *source, esp_display_present_engine_rotation_t rotation)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!spi) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
        };
    }
    size_t bytes;
    esp_err_t ret = validate_tx(spi, surface, area, cookie, &bytes, staged);
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
    if (spi->count == spi->config.max_inflight || (staged && spi->count)) {
        present_lock_exit(&spi->lock, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, spi->config.region_mode ? ESP_ERR_NOT_FINISHED : ESP_ERR_NO_MEM
        };
    }
    present_lock_exit(&spi->lock, false);

    if (staged && !spi->stage.slots[0].pixels) {
        void *first = present_alloc_pixels(spi->config.staging_bytes);
        void *second = present_alloc_pixels(spi->config.staging_bytes);
        if (!first || !second) {
            present_free(first);
            present_free(second);
            return (esp_display_present_tx_result_t) {
                ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM
            };
        }
        spi->stage.slots[0].pixels = first;
        spi->stage.slots[1].pixels = second;
    }
    ret = staged ? ESP_OK : esp_cache_msync(surface->pixels, bytes,
                                            ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    /* esp_cache_msync documents NOT_SUPPORTED for a non-cacheable range. */
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ret
        };
    }
    present_lock_enter(&spi->lock, false);
    /* stop can race the cache operation above; never enqueue after detach/abort. */
    if (!spi->attached || spi->closing || spi->submitting || spi->count == spi->config.max_inflight || (staged && spi->count)) {
        present_lock_exit(&spi->lock, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    spi->stage.staged = staged;
    spi->stage.result = ESP_OK;
    spi->stage.started = spi->stage.result_finalized = false;
    spi->cookies[(spi->head + spi->count) % spi->config.max_inflight] = cookie;
    ++spi->count;
    spi->submitting = true;
    present_lock_exit(&spi->lock, false);

    if (staged) {
        ret = stage_transfer(spi, surface, area, source, rotation);
    } else {
        ret = esp_lcd_panel_draw_bitmap(spi->config.panel, (int)area->x, (int)area->y,
                                        (int)(area->x + area->width), (int)(area->y + area->height),
                                        surface->pixels);
    }
    present_lock_enter(&spi->lock, false);
    /* Commit the transaction outcome with submitting, before publishing. A
     * later stop must not retroactively turn a completed success into failure. */
    if (staged) {
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
    if (staged) {
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

/* Pin task-side preflight/cache/allocation as well as the driver call. A
 * concurrent quiesce must join this entire lifetime before returning success. */
static bool begin_call(esp_display_present_lcd_io_t *spi)
{
    if (!spi) {
        return false;
    }
    present_lock_enter(&spi->lock, false);
    bool allowed = spi->attached && !spi->closing && !spi->active_calls;
    if (allowed) {
        ++spi->active_calls;
    }
    present_lock_exit(&spi->lock, false);
    return allowed;
}

static esp_display_present_tx_result_t end_call(esp_display_present_lcd_io_t *spi, esp_display_present_tx_result_t result)
{
    present_lock_enter(&spi->lock, false);
    --spi->active_calls;
    present_lock_exit(&spi->lock, false);
    return result;
}

static esp_display_present_tx_result_t spi_submit(void *context, const esp_display_present_surface_t *surface,
                                                  const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!begin_call(spi)) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    return end_call(spi, submit_pixels(context, surface, area, cookie, spi->config.staging_bytes != 0,
                                       NULL, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0));
}

/* Direct buffers are word-aligned; may_alias permits access to pixel bytes. */
static inline void swap_rgb565_word(uint8_t *pixels)
{
    typedef uint32_t pixel_word_t __attribute__((__may_alias__));
    pixel_word_t *word = (pixel_word_t *)pixels;
    uint32_t value = *word;
    value = ((value & 0xff00ff00U) >> 8) | ((value & 0x00ff00ffU) << 8);
    *word = value;
}

static void swap_rgb565(void *pixels, size_t bytes)
{
    /* Direct-buffer validation establishes alignment and an even byte count.
     * Batch 16 pixels, then handle pairs and a possible final single pixel. */
    uint8_t *p = pixels;
    while (bytes >= 32) {
        swap_rgb565_word(p);
        swap_rgb565_word(p + 4);
        swap_rgb565_word(p + 8);
        swap_rgb565_word(p + 12);
        swap_rgb565_word(p + 16);
        swap_rgb565_word(p + 20);
        swap_rgb565_word(p + 24);
        swap_rgb565_word(p + 28);
        p += 32;
        bytes -= 32;
    }
    while (bytes >= 4) {
        swap_rgb565_word(p);
        p += 4;
        bytes -= 4;
    }
    if (bytes) {
        uint8_t value = p[0]; p[0] = p[1]; p[1] = value;
    }
}

static esp_display_present_tx_result_t emit_pixels(void *context, const esp_display_present_surface_t *source,
                                                   const esp_display_present_rect_t *area,
                                                   esp_display_present_engine_rotation_t rotation, uint64_t cookie)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!spi || !spi->config.region_mode || !source || !area || !source->pixels ||
            !source->layout.width || !source->layout.height ||
            source->layout.format != spi->config.format ||
            rotation < ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || rotation > ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270 ||
            (source->layout.byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE && source->layout.byte_order != ESP_DISPLAY_PRESENT_ORDER_SWAPPED) ||
            (spi->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB888 && source->layout.byte_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE)) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
        };
    }
    size_t bpp = spi->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (source->layout.width > SIZE_MAX / bpp) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_SIZE
        };
    }
    size_t row = (size_t)source->layout.width * bpp;
    size_t stride = source->layout.stride_bytes;
    if (stride < row || row > source->capacity_bytes ||
            source->layout.height - 1 > (source->capacity_bytes - row) / stride ||
            (uintptr_t)source->pixels > UINTPTR_MAX - source->capacity_bytes) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_SIZE
        };
    }
    bool quarter = rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270;
    esp_display_present_surface_t wire = *source;
    wire.layout.width = quarter ? source->layout.height : source->layout.width;
    wire.layout.height = quarter ? source->layout.width : source->layout.height;
    wire.layout.stride_bytes = (size_t)wire.layout.width * bpp;
    wire.layout.byte_order = spi->config.byte_order;
    size_t bytes = 0;
    /* Direct legality is checked per request, never imposed on frontend dirties. */
    bool direct = rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 && stride == row &&
                  validate_tx(spi, &wire, area, cookie, &bytes, false) == ESP_OK;
    bool swapped = direct && source->layout.byte_order != spi->config.byte_order;
    if (swapped) {
        swap_rgb565(source->pixels, bytes);
    }
    esp_display_present_tx_result_t result = submit_pixels(spi, &wire, area, cookie, !direct,
                                                           direct ? NULL : source, rotation);
    if (swapped && result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED) {
        swap_rgb565(source->pixels, bytes);
    }
    return result;
}

static esp_display_present_tx_result_t spi_emit(void *context, const esp_display_present_surface_t *source,
                                                const esp_display_present_rect_t *area,
                                                esp_display_present_engine_rotation_t rotation, uint64_t cookie)
{
    esp_display_present_lcd_io_t *spi = context;
    if (!begin_call(spi)) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    return end_call(spi, emit_pixels(context, source, area, rotation, cookie));
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
        bool busy = spi->count || spi->active_callbacks || spi->active_calls || spi->submitting;
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
        if (!config->region_mode) {
            spi->config.max_inflight = 1;
        }
        if (!config->region_mode) {
            spi->stage.slots[0].pixels = present_alloc_pixels(config->staging_bytes);
            spi->stage.slots[1].pixels = present_alloc_pixels(config->staging_bytes);
        }
        spi->stage.event = present_event_create();
        if ((!config->region_mode && (!spi->stage.slots[0].pixels || !spi->stage.slots[1].pixels)) || !spi->stage.event) {
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
        spi, spi_attach, spi_submit, spi_quiesce, spi_request_abort, spi->config.region_mode ? spi_emit : NULL
    };
    return ESP_OK;
}

esp_err_t esp_display_present_lcd_io_destroy(esp_display_present_lcd_io_t *spi)
{
    if (!spi) {
        return ESP_ERR_INVALID_ARG;
    }
    if (spi->attached || spi->count || spi->active_callbacks || spi->active_calls || spi->submitting) {
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
    bool direct_external_dma = spi->config.psram_dma_direct && !spi->config.staging_bytes && !spi->config.region_mode;
    *out = (esp_display_present_output_caps_t) {
        .x_alignment = 1,
        .y_alignment = 1,
        .w_align = 1,
        .h_align = 1,
        .base_alignment = PRESENT_SPI_BASE_ALIGNMENT,
        .stride_align = 1,
        .tight_stride = true,
        .full_only = false,
        .internal_dma = true,
        .ext_mem = spi->config.psram_dma_direct || spi->config.staging_bytes != 0,
        .max_inflight = spi->config.max_inflight,
        .ext_len_align = direct_external_dma ? PRESENT_SPI_EXTERNAL_LENGTH_ALIGNMENT : 1,
    };
    return ESP_OK;
}

size_t esp_display_present_lcd_io_memory(size_t staging_bytes)
{
    if (staging_bytes > (SIZE_MAX - sizeof(esp_display_present_lcd_io_t) - present_event_memory()) / 2 - PRESENT_SPI_BASE_ALIGNMENT) {
        return SIZE_MAX;
    }
    size_t aligned = (staging_bytes + PRESENT_SPI_BASE_ALIGNMENT - 1) & ~(size_t)(PRESENT_SPI_BASE_ALIGNMENT - 1);
    return sizeof(esp_display_present_lcd_io_t) + (staging_bytes ? 2 * aligned + present_event_memory() : 0);
}
