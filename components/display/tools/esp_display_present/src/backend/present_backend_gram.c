/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_memory.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_frame_core.h"
#include "present_transaction.h"
#include "present_allocation.h"

/* A slot owns both its draw tile and optional converted tile. Successful copy
 * does not release the draw tile independently; the pair is recycled together. */
typedef enum {
    SLOT_FREE,       /* Available to acquire. */
    SLOT_LEASED,     /* Producer may write the draw tile. */
    SLOT_COPYING,    /* Pixel engine is preparing the transfer tile. */
    SLOT_READY,      /* Pixels are prepared; waiting for submission capacity. */
    SLOT_SUBMITTING, /* Adapter call in progress; completion may arrive early. */
    SLOT_INFLIGHT,   /* Transfer accepted or uncertain; await real completion. */
} slot_state_t;
typedef present_core_frame_t frame_record_t;

typedef struct {
    void *pixels, *converted;
    present_allocation_t storage; /* Source/conversion pair is one reusable tile slot. */
    esp_display_present_lease_t lease;
    uint64_t cookie;
    size_t frame;
    slot_state_t state;
    present_transaction_t transaction;
    esp_display_present_copy_ticket_t copy;
    esp_display_present_surface_t tx;
    esp_display_present_rect_t area;
} gram_slot_t;

struct present_backend {
    present_lock_t lock;
    esp_display_present_backend_config_t config;
    esp_display_present_pixel_engine_t *pixel;
    gram_slot_t *slots;
    present_frame_core_t core;
    size_t tile_bytes, next_slot;
};

static bool valid_area(const present_backend_t *backend, const esp_display_present_rect_t *area)
{
    return present_valid_rect(backend->config.width, backend->config.height, area);
}

static frame_record_t *find_frame(present_backend_t *backend, uint64_t token)
{
    return present_frame_core_find(&backend->core, token);
}

static gram_slot_t *find_lease(present_backend_t *backend, uint64_t token, size_t frame)
{
    if (!token) {
        return NULL;
    }
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        gram_slot_t *slot = &backend->slots[i];
        if (slot->state == SLOT_LEASED && slot->lease.id.value == token && slot->frame == frame) {
            return slot;
        }
    }
    return NULL;
}

/* Called under backend lock; preserve first failure even after auto-recycle. */
static void PRESENT_ISR_ATTR record_fault(present_backend_t *backend, frame_record_t *record,
                                          uint64_t cookie, esp_err_t error)
{
    present_frame_core_fail(&backend->core, record, cookie, error);
}

static void PRESENT_ISR_ATTR complete_slot(present_backend_t *backend, gram_slot_t *slot)
{
    frame_record_t *record = &backend->core.frames[slot->frame];
    if (slot->storage.readers[PRESENT_REF_COPY]) {
        present_allocation_drop(&slot->storage, PRESENT_REF_COPY);
    }
    present_allocation_drop(&slot->storage, PRESENT_REF_TRANSFER);
    slot->state = SLOT_FREE;
    slot->cookie = 0;
    present_frame_core_complete(&backend->core, record);
}

static bool copy_done(void *context, esp_display_present_copy_ticket_t ticket, const esp_display_present_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    present_backend_t *backend = context;
    return backend->config.on_progress ? backend->config.on_progress(backend->config.progress_user, isr) : false;
}

/* Lock held; ready copies are not hardware transfers. */
static bool channel_full(const present_backend_t *backend)
{
    size_t used = 0, limit = backend->config.transfer_capacity ? backend->config.transfer_capacity : backend->config.buffer_count;
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        used += backend->slots[i].state == SLOT_SUBMITTING || backend->slots[i].state == SLOT_INFLIGHT;
    }
    return used >= limit;
}

esp_err_t present_gram_service(present_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    for (;;) {
        present_lock_enter(&backend->lock, false);
        gram_slot_t *slot = NULL;
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *candidate = &backend->slots[i];
            if ((candidate->state == SLOT_COPYING || candidate->state == SLOT_READY) &&
                    (!slot || candidate->cookie < slot->cookie)) {
                slot = candidate;
            }
        }
        if (!slot) {
            present_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        bool copying = slot->state == SLOT_COPYING;
        present_lock_exit(&backend->lock, false);
        esp_err_t copy_result = ESP_OK;
        if (copying) {
            esp_display_present_copy_status_t status;
            esp_err_t error = esp_display_present_pixel_wait(backend->pixel, slot->copy, 0, &status);
            if (error == ESP_ERR_TIMEOUT) {
                return ESP_OK;    /* Keep submission order. */
            }
            if (error != ESP_OK) {
                return error;
            }
            copy_result = status.result;
            (void)esp_display_present_pixel_release(backend->pixel, slot->copy);
            slot->copy.value = 0;
        }
        present_lock_enter(&backend->lock, false);
        frame_record_t *record = &backend->core.frames[slot->frame];
        if (copying) {
            present_allocation_drop(&slot->storage, PRESENT_REF_COPY);
        }
        record_fault(backend, record, slot->cookie, copy_result);
        if (record->canceled || backend->core.life != PRESENT_CORE_RUNNING || backend->core.fault.result != ESP_OK) {
            if (!record->canceled && record->common.result == ESP_OK) {
                record_fault(backend, record, slot->cookie, backend->core.fault.result != ESP_OK ? backend->core.fault.result : ESP_ERR_INVALID_STATE);
            }
            complete_slot(backend, slot);
            present_lock_exit(&backend->lock, false);
            continue;
        }
        if (channel_full(backend)) {
            slot->state = SLOT_READY;
            present_lock_exit(&backend->lock, false);
            return ESP_ERR_NO_MEM;
        }
        slot->state = SLOT_SUBMITTING;
        present_transaction_prepare(&slot->transaction);
        present_lock_exit(&backend->lock, false);
        esp_display_present_tx_result_t result = backend->config.panel.submit(backend->config.panel.context, &slot->tx, &slot->area, slot->cookie);
        present_lock_enter(&backend->lock, false);
        present_transaction_resolution_t resolved = present_transaction_resolve(&slot->transaction, result);
        if (resolved.rejected) {
            slot->state = SLOT_READY; /* Accepted copy stays owned; retry by service. */
            present_lock_exit(&backend->lock, false);
            return result.result == ESP_OK ? ESP_FAIL : result.result;
        }
        record_fault(backend, record, slot->cookie, resolved.result);
        if (resolved.complete) {
            complete_slot(backend, slot);
        } else {
            slot->state = SLOT_INFLIGHT;
        }
        present_lock_exit(&backend->lock, false);
    }
}

static bool PRESENT_ISR_ATTR event_dispatch(void *user, const esp_display_present_backend_event_t *event, bool isr)
{
    present_backend_t *backend = user;
    if (!backend || !event || !event->transaction_cookie) {
        return false;
    }
    present_lock_enter(&backend->lock, isr);
    if (backend->core.life != PRESENT_CORE_QUIESCENT) {
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *slot = &backend->slots[i];
            if (!slot->cookie || slot->cookie != event->transaction_cookie ||
                    (slot->state != SLOT_SUBMITTING && slot->state != SLOT_INFLIGHT)) {
                continue;
            }
            frame_record_t *record = &backend->core.frames[slot->frame];
            if (event->kind == ESP_DISPLAY_PRESENT_EVENT_FAULT) {
                record_fault(backend, record, slot->cookie, event->result != ESP_OK ? event->result : ESP_FAIL);
                break; /* Fault does NOT establish that hardware stopped. */
            }
            if (event->kind != ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE) {
                break;
            }
            if (!present_transaction_done(&slot->transaction, event->result)) {
                record_fault(backend, record, slot->cookie, ESP_ERR_INVALID_STATE);
                break;
            }
            record_fault(backend, record, slot->cookie, event->result);
            if (slot->state == SLOT_INFLIGHT) {
                complete_slot(backend, slot);
            }
            break;
        }
    }
    present_lock_exit(&backend->lock, isr);
    return backend->config.on_progress ? backend->config.on_progress(backend->config.progress_user, isr) : false;
}

static bool PRESENT_ISR_ATTR gram_on_event_isr(present_backend_t *backend, const esp_display_present_backend_event_t *event)
{
    return event_dispatch(backend, event, true);
}

static void gram_on_event(present_backend_t *backend, const esp_display_present_backend_event_t *event)
{
    (void)event_dispatch(backend, event, false);
}

static void free_backend(present_backend_t *backend)
{
    if (backend->slots) {
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            present_free(backend->slots[i].pixels);
            present_free(backend->slots[i].converted);
        }
    }
    if (backend->pixel) {
        (void)esp_display_present_pixel_stop(backend->pixel, UINT32_MAX);
        (void)esp_display_present_pixel_destroy(backend->pixel);
    }
    present_frame_core_deinit(&backend->core);
    present_free(backend->slots);
    present_lock_destroy(&backend->lock);
    present_free(backend);
}

static esp_err_t gram_create(const esp_display_present_backend_config_t *config, present_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (config && config->copy_execute && !config->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config && ((config->tx_staging && !config->async_copy) ||
                   ((config->async_copy || config->rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0) && config->in_place))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!config || !config->panel.attach || !config->panel.submit || !config->panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->width || !config->height || !config->tile_rows || config->tile_rows > config->height ||
            !config->buffer_count || config->buffer_count > PRESENT_MAX_TILE_BUFFERS || !config->max_frames || config->max_frames > PRESENT_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB565 && config->format != ESP_DISPLAY_PRESENT_FORMAT_RGB888) ||
            (config->draw_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE && config->draw_order != ESP_DISPLAY_PRESENT_ORDER_SWAPPED) ||
            (config->panel_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE && config->panel_order != ESP_DISPLAY_PRESENT_ORDER_SWAPPED) ||
            (config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB888 && (config->draw_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE || config->panel_order != ESP_DISPLAY_PRESENT_ORDER_NATIVE))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->render_contract != ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION || (config->rotation < ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || config->rotation > ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270) || config->scan_buf || config->ext_bufs || config->ext_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = config->format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (config->width > SIZE_MAX / bytes || config->tile_rows > SIZE_MAX / (config->width * bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    present_backend_t *backend = present_alloc(sizeof(*backend));
    if (!backend) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&backend->lock)) {
        present_free(backend);
        return ESP_ERR_NO_MEM;
    }
    backend->config = *config;
    backend->core.active = SIZE_MAX;
    backend->tile_bytes = config->width * bytes * config->tile_rows;
    backend->slots = present_alloc(config->buffer_count * sizeof(*backend->slots));
    esp_err_t core_error = present_frame_core_init(&backend->core, config->max_frames);
    if (!backend->slots || core_error != ESP_OK) {
        free_backend(backend);
        return ESP_ERR_NO_MEM;
    }
    bool convert = config->rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || config->async_copy || (config->draw_order != config->panel_order && !config->in_place);
    for (size_t i = 0; i < config->buffer_count; ++i) {
        backend->slots[i].pixels = present_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram);
        if (convert) {
            backend->slots[i].converted = present_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram && !backend->config.tx_staging);
        }
        if (!backend->slots[i].pixels || (convert && !backend->slots[i].converted)) {
            free_backend(backend);
            return ESP_ERR_NO_MEM;
        }
    }
    esp_display_present_pixel_config_t pixel_config = {
        .max_jobs = config->buffer_count,
        .asynchronous = config->async_copy,
        .execute = config->copy_execute,
        .execute_user = config->copy_user,
        .on_event = config->async_copy ? copy_done : NULL,
        .user = backend,
    };
    esp_err_t ret = esp_display_present_pixel_create(&pixel_config, &backend->pixel);
    if (ret != ESP_OK) {
        free_backend(backend);
        return ret;
    }
    ret = config->panel.attach(config->panel.context, event_dispatch, backend);
    if (ret != ESP_OK) {
        free_backend(backend);
        return ret;
    }
    *out = backend;
    return ESP_OK;
}

static esp_err_t gram_get_caps(present_backend_t *backend, esp_display_present_backend_caps_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = backend->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    *out = (esp_display_present_backend_caps_t) {
        .contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
        .draw_layout = { backend->config.width, backend->config.height, backend->config.width * bytes,
                         backend->config.format, backend->config.draw_order
                       },
        .drawbuf_capacity_bytes = backend->tile_bytes,
        .buffer_alignment = PRESENT_BUFFER_ALIGNMENT,
        .max_tile_rows = backend->config.tile_rows,
        .max_damage_areas = PRESENT_MAX_DAMAGE_AREAS,
    };
    return ESP_OK;
}

static esp_err_t gram_begin(present_backend_t *backend, const esp_display_present_frame_request_t *request, esp_display_present_frame_t *out)
{
    if (!backend) {
        if (out) {
            out->value = 0;
        }
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = present_frame_core_admit(&backend->core, &backend->lock, backend->config.width, backend->config.height, request, out);
    if (ret != ESP_OK) {
        return ret;
    }
    present_lock_enter(&backend->lock, false);
    ret = present_frame_core_begin(&backend->core, out);
    present_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_acquire(present_backend_t *backend, esp_display_present_frame_t frame, const esp_display_present_rect_t *area,
                              uint32_t timeout, esp_display_present_lease_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (esp_display_present_lease_t) {
        0
    };
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_display_present_rect_t default_area = { 0, 0, backend->config.width, backend->config.tile_rows };
    const esp_display_present_rect_t *req_area = area ? area : &default_area;
    if (!valid_area(backend, req_area)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    for (;;) {
        (void)present_gram_service(backend);
        present_lock_enter(&backend->lock, false);
        frame_record_t *record = find_frame(backend, frame.value);
        if (backend->core.life != PRESENT_CORE_RUNNING || !record || record->sealed || record->common.result != ESP_OK) {
            present_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *slot = &backend->slots[(backend->next_slot + i) % backend->config.buffer_count];
            if (slot->state != SLOT_FREE) {
                continue;
            }
            uint64_t token = present_new_token();
            if (!token) {
                present_lock_exit(&backend->lock, false);
                return ESP_ERR_NO_MEM;
            }
            backend->next_slot = ((size_t)(slot - backend->slots) + 1) % backend->config.buffer_count;
            esp_display_present_rect_t actual = *req_area;
            if (actual.height > backend->config.tile_rows) {
                actual.height = backend->config.tile_rows;
            }
            size_t bytes = backend->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
            slot->lease = (esp_display_present_lease_t) {
                .id = { token },
                .logical_area = actual,
                .surface = { slot->pixels, backend->tile_bytes,
                    { actual.width, actual.height, actual.width * bytes, backend->config.format, backend->config.draw_order }
                }
            };
            slot->frame = (size_t)(record - backend->core.frames);
            present_allocation_claim(&slot->storage);
            slot->state = SLOT_LEASED;
            ++record->leased;
            *out = slot->lease;
            present_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        present_lock_exit(&backend->lock, false);
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        if (backend->config.on_progress) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
}

static void revert_in_place_swap(void *pixels, size_t byte_count)
{
    uint8_t *byte_data = pixels;
    size_t i = 0;
    if (((uintptr_t)byte_data & 3u) == 0) {
        uint32_t *p32 = (uint32_t *)byte_data;
        for (; i + 4 <= byte_count; i += 4, ++p32) {
            uint32_t v = *p32;
            *p32 = ((v & 0x00ff00ffu) << 8) | ((v & 0xff00ff00u) >> 8);
        }
    }
    for (; i + 1 < byte_count; i += 2) {
        uint8_t tmp = byte_data[i];
        byte_data[i] = byte_data[i + 1];
        byte_data[i + 1] = tmp;
    }
}

static esp_err_t prepare_pixels(present_backend_t *backend, gram_slot_t *slot,
                                const esp_display_present_rect_t *area, size_t src_stride,
                                esp_display_present_surface_t *out, bool *out_swapped_in_place)
{
    size_t bytes = backend->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t tight_stride = area->width * bytes;
    bool need_swap = backend->config.draw_order != backend->config.panel_order;
    bool need_pack = src_stride != tight_stride;
    bool quarter = backend->config.rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || backend->config.rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270;
    uint32_t dw = quarter ? area->height : area->width, dh = quarter ? area->width : area->height;
    *out_swapped_in_place = false;
    *out = (esp_display_present_surface_t) {
        .pixels = slot->pixels,
        .capacity_bytes = backend->tile_bytes,
        .layout = { dw, dh, dw * bytes,
                    backend->config.format, backend->config.panel_order
                  },
    };
    if (!need_swap && !need_pack && backend->config.rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0) {
        return ESP_OK;
    }
    if (!need_pack && backend->config.in_place && backend->config.rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 && bytes == 2) {
        revert_in_place_swap(slot->pixels, (size_t)area->height * tight_stride);
        *out_swapped_in_place = true;
        return ESP_OK;
    }
    if (need_pack || !backend->config.in_place || backend->config.rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0) {
        if (!slot->converted) {
            slot->converted = present_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram && !backend->config.tx_staging);
            if (!slot->converted) {
                return ESP_ERR_NO_MEM;
            }
        }
        out->pixels = slot->converted;
    }
    esp_display_present_copy_request_t request = {
        .src = {
            .pixels = slot->pixels,
            .capacity_bytes = backend->tile_bytes,
            .layout = {
                area->width, area->height, src_stride,
                backend->config.format, backend->config.draw_order
            },
        },
        .dst = *out,
        .src_area = { 0, 0, area->width, area->height },
        .dst_x = 0,
        .dst_y = 0,
        .rotation = backend->config.rotation,
    };
    esp_display_present_copy_ticket_t ticket;
    esp_err_t ret = esp_display_present_pixel_submit(backend->pixel, &request, &ticket);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_display_present_copy_status_t status;
    ret = esp_display_present_pixel_wait(backend->pixel, ticket, UINT32_MAX, &status);
    if (ret == ESP_OK) {
        ret = status.result;
    }
    (void)esp_display_present_pixel_release(backend->pixel, ticket);
    if (ret == ESP_OK && out->pixels == slot->pixels && need_swap) {
        *out_swapped_in_place = true;
    }
    return ret;
}

typedef struct {
    frame_record_t *record;
    gram_slot_t *slot;
    esp_display_present_rect_t submit_area;
    esp_display_present_rect_t physical_area;
    size_t bytes;
    size_t src_stride;
    size_t row_bytes;
} gram_submit_context_t;

static void revert_submit_swap(const gram_submit_context_t *ctx, bool swapped_in_place)
{
    if (swapped_in_place) {
        revert_in_place_swap(ctx->slot->pixels, ctx->submit_area.height * ctx->row_bytes);
    }
}

/* Lock is not held on return. The direct-sync channel check stays here, before
 * unlock, so a full channel never reaches pixel preparation. */
static esp_err_t gram_prepare_submit_context(present_backend_t *backend, esp_display_present_frame_t frame,
                                             esp_display_present_lease_id_t lease, const esp_display_present_region_submit_t *sub,
                                             gram_submit_context_t *ctx)
{
    present_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    gram_slot_t *slot = record ? find_lease(backend, lease.value, (size_t)(record - backend->core.frames)) : NULL;
    if (backend->core.life != PRESENT_CORE_RUNNING || !record || record->sealed || record->common.result != ESP_OK || !slot) {
        present_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    esp_display_present_rect_t submit_area = slot->lease.logical_area;
    size_t bytes = backend->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t src_stride = slot->lease.surface.layout.stride_bytes;
    if (sub) {
        if (sub->logical_area.width || sub->logical_area.height ||
                sub->logical_area.x || sub->logical_area.y) {
            submit_area = sub->logical_area;
        }
        if (sub->stride_bytes) {
            src_stride = sub->stride_bytes;
        } else {
            src_stride = submit_area.width * bytes;
        }
    }
    size_t row_bytes = submit_area.width * bytes;
    if (!valid_area(backend, &submit_area) || src_stride < row_bytes || src_stride % bytes ||
            submit_area.height - 1 > (SIZE_MAX - row_bytes) / src_stride ||
            (submit_area.height - 1) * src_stride + row_bytes > backend->tile_bytes) {
        present_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_ARG;
    }
    if (!backend->config.async_copy && !backend->config.deferred_submit && channel_full(backend)) {
        present_lock_exit(&backend->lock, false);
        return ESP_ERR_NO_MEM;
    }
    present_lock_exit(&backend->lock, false);
    esp_display_present_rect_t physical_area;
    esp_err_t geometry = esp_display_present_rotate_rect(backend->config.width, backend->config.height, backend->config.rotation, &submit_area, &physical_area);
    if (geometry != ESP_OK) {
        return geometry;
    }
    *ctx = (gram_submit_context_t) {
        record, slot, submit_area, physical_area, bytes, src_stride, row_bytes
    };
    return ESP_OK;
}

static esp_err_t gram_submit_async_copy(present_backend_t *backend, const gram_submit_context_t *ctx)
{
    uint64_t cookie = present_new_token();
    if (!cookie) {
        return ESP_ERR_NO_MEM;
    }
    esp_display_present_surface_t target = {ctx->slot->converted, backend->tile_bytes,
        {ctx->physical_area.width, ctx->physical_area.height, ctx->physical_area.width * ctx->bytes, backend->config.format, backend->config.panel_order}
    };
    esp_display_present_copy_request_t request = {
        .src = {
            ctx->slot->pixels, backend->tile_bytes,
            {ctx->submit_area.width, ctx->submit_area.height, ctx->src_stride, backend->config.format, backend->config.draw_order}
        },
        .dst = target,
        .src_area = {0, 0, ctx->submit_area.width, ctx->submit_area.height},
        .rotation = backend->config.rotation
    };
    esp_display_present_copy_ticket_t ticket;
    esp_err_t error = esp_display_present_pixel_submit(backend->pixel, &request, &ticket);
    if (error != ESP_OK) {
        return error;
    }
    /* Task APIs serialized. Callback only wakes, never accesses this slot. */
    present_lock_enter(&backend->lock, false);
    ctx->slot->copy = ticket;
    ctx->slot->cookie = cookie;
    ctx->slot->tx = target;
    ctx->slot->area = ctx->physical_area;
    present_allocation_written(&ctx->slot->storage);
    present_allocation_hold(&ctx->slot->storage, PRESENT_REF_COPY);
    present_allocation_hold(&ctx->slot->storage, PRESENT_REF_TRANSFER);
    ctx->slot->state = SLOT_COPYING;
    --ctx->record->leased;
    ++ctx->record->pending;
    present_lock_exit(&backend->lock, false);
    return ESP_OK;
}

/* Slot is already SUBMITTING and the lock is released. Driver rejection returns
 * the lease and undoes an in-place swap. Service rejection keeps READY instead. */
static esp_err_t gram_submit_prepared_direct(present_backend_t *backend, gram_submit_context_t *ctx,
                                             const esp_display_present_surface_t *tx, uint64_t cookie, bool swapped_in_place)
{
    esp_display_present_tx_result_t result = backend->config.panel.submit(backend->config.panel.context, tx, &ctx->physical_area, cookie);
    present_lock_enter(&backend->lock, false);
    present_transaction_resolution_t resolved = present_transaction_resolve(&ctx->slot->transaction, result);
    if (resolved.rejected) {
        --ctx->record->pending;
        present_allocation_drop(&ctx->slot->storage, PRESENT_REF_TRANSFER);
        present_allocation_claim(&ctx->slot->storage);
        ctx->slot->state = SLOT_LEASED;
        ctx->slot->cookie = 0;
        esp_err_t ret = result.result != ESP_OK ? result.result : ESP_FAIL;
        present_lock_exit(&backend->lock, false);
        revert_submit_swap(ctx, swapped_in_place);
        return ret;
    }
    /* Early completion proves acceptance even if the adapter returned an
     * inconsistent rejection. Uncertain acceptance never returns a lease. */
    --ctx->record->leased;
    record_fault(backend, ctx->record, cookie, resolved.result);
    if (resolved.complete) {
        complete_slot(backend, ctx->slot);
    } else {
        ctx->slot->state = SLOT_INFLIGHT;
    }
    present_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t gram_submit_sync_pixels(present_backend_t *backend, gram_submit_context_t *ctx)
{
    esp_display_present_surface_t tx;
    bool swapped_in_place = false;
    esp_err_t ret = prepare_pixels(backend, ctx->slot, &ctx->submit_area, ctx->src_stride, &tx, &swapped_in_place);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t cookie = present_new_token();
    if (!cookie) {
        revert_submit_swap(ctx, swapped_in_place);
        return ESP_ERR_NO_MEM;
    }
    present_lock_enter(&backend->lock, false);
    /* An older transfer may have faulted while the CPU was converting. */
    if (ctx->record->common.result != ESP_OK) {
        present_lock_exit(&backend->lock, false);
        revert_submit_swap(ctx, swapped_in_place);
        return ESP_ERR_INVALID_STATE;
    }
    if (!backend->config.deferred_submit && channel_full(backend)) {
        present_lock_exit(&backend->lock, false);
        revert_submit_swap(ctx, swapped_in_place);
        return ESP_ERR_NO_MEM;
    }
    present_allocation_written(&ctx->slot->storage);
    present_allocation_hold(&ctx->slot->storage, PRESENT_REF_TRANSFER);
    ctx->slot->state = backend->config.deferred_submit ? SLOT_READY : SLOT_SUBMITTING;
    ctx->slot->cookie = cookie;
    ctx->slot->tx = tx;
    ctx->slot->area = ctx->physical_area;
    present_transaction_prepare(&ctx->slot->transaction);
    ++ctx->record->pending;
    if (backend->config.deferred_submit) {
        --ctx->record->leased;
        present_lock_exit(&backend->lock, false);
        if (backend->config.on_progress) {
            (void)backend->config.on_progress(backend->config.progress_user, false);
        }
        return ESP_OK;
    }
    present_lock_exit(&backend->lock, false);
    return gram_submit_prepared_direct(backend, ctx, &tx, cookie, swapped_in_place);
}

static esp_err_t gram_submit_region_ex(present_backend_t *backend, esp_display_present_frame_t frame,
                                       esp_display_present_lease_id_t lease,
                                       const esp_display_present_region_submit_t *sub)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    gram_submit_context_t ctx;
    esp_err_t error = gram_prepare_submit_context(backend, frame, lease, sub, &ctx);
    if (error != ESP_OK) {
        return error;
    }
    if (backend->config.async_copy) {
        return gram_submit_async_copy(backend, &ctx);
    }
    return gram_submit_sync_pixels(backend, &ctx);
}

static esp_err_t gram_submit_region(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease)
{
    return gram_submit_region_ex(backend, frame, lease, NULL);
}

static esp_err_t gram_cancel_region(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_lease_id_t lease)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    gram_slot_t *slot = record ? find_lease(backend, lease.value, (size_t)(record - backend->core.frames)) : NULL;
    if (!record || record->sealed || !slot) {
        present_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    --record->leased;
    present_allocation_written(&slot->storage);
    slot->state = SLOT_FREE;
    present_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t gram_get_draw_buffers(present_backend_t *backend, void **out_buffers,
                                       size_t max_buffers, size_t *out_count,
                                       size_t *out_buffer_bytes)
{
    if (!backend || !out_count || (max_buffers && !out_buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = backend->config.buffer_count;
    if (out_buffer_bytes) {
        *out_buffer_bytes = backend->tile_bytes;
    }
    if (max_buffers < backend->config.buffer_count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        out_buffers[i] = backend->slots[i].pixels;
    }
    return ESP_OK;
}

static esp_err_t gram_end_ex(present_backend_t *backend, esp_display_present_frame_t frame,
                             const esp_display_present_frame_coverage_t *coverage,
                             esp_display_present_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    if (coverage && present_validate_damage(backend->config.width, backend->config.height,
                                            coverage->areas, coverage->area_count) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    esp_err_t ret = present_frame_core_seal(&backend->core, record, out);
    present_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_end(present_backend_t *backend, esp_display_present_frame_t frame, esp_display_present_receipt_t *out)
{
    return gram_end_ex(backend, frame, NULL, out);
}

static void cancel_build(present_backend_t *backend, size_t index)
{
    frame_record_t *record = &backend->core.frames[index];
    record->sealed = true;
    record->canceled = true;
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        gram_slot_t *slot = &backend->slots[i];
        if (slot->state == SLOT_LEASED && slot->frame == index) {
            present_allocation_written(&slot->storage);
            slot->state = SLOT_FREE;
            --record->leased;
        }
    }
}

static esp_err_t gram_cancel_frame(present_backend_t *backend, esp_display_present_frame_t frame, uint32_t timeout)
{
    if (!backend || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    for (;;) {
        present_lock_enter(&backend->lock, false);
        frame_record_t *record = find_frame(backend, frame.value);
        if (!record) {
            if (present_cancel_matches(&backend->core.canceled, frame.value)) {
                present_lock_exit(&backend->lock, false);
                return ESP_OK;
            }
            present_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        if (record->common.external || (record->sealed && !record->canceled)) {
            present_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        cancel_build(backend, (size_t)(record - backend->core.frames));
        if (!record->pending) {
            present_frame_core_reap(&backend->core, record);
            present_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        present_lock_exit(&backend->lock, false);
        (void)present_gram_service(backend);
        present_lock_enter(&backend->lock, false);
        bool canceled = present_cancel_matches(&backend->core.canceled, frame.value);
        present_lock_exit(&backend->lock, false);
        if (canceled) {
            return ESP_OK;
        }
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        present_pause();
    }
}

static esp_err_t gram_query(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_frame_status_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)present_gram_service(backend);
    return present_frame_core_query(&backend->core, &backend->lock, receipt, false, out);
}

static esp_err_t gram_get_fault(present_backend_t *backend, esp_display_present_backend_fault_t *out)
{
    return backend ? present_frame_core_get_fault(&backend->core, &backend->lock, out) : ESP_ERR_INVALID_ARG;
}

static esp_err_t gram_wait(present_backend_t *backend, esp_display_present_receipt_t receipt, esp_display_present_completion_point_t point,
                           uint32_t timeout, esp_display_present_frame_status_t *out)
{
    return present_frame_core_wait(backend, gram_query, receipt, point, false, timeout, out);
}

static esp_err_t gram_release_receipt(present_backend_t *backend, esp_display_present_receipt_t receipt)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    return present_frame_core_release_receipt(&backend->core, &backend->lock, receipt);
}

static esp_err_t gram_stop(present_backend_t *backend, uint32_t timeout)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&backend->lock, false);
    if (backend->core.life == PRESENT_CORE_QUIESCENT) {
        present_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    backend->core.life = PRESENT_CORE_CLOSING;
    if (backend->core.active != SIZE_MAX) {
        cancel_build(backend, backend->core.active);
    }
    present_lock_exit(&backend->lock, false);
    uint64_t start = present_now_ms();
    esp_err_t ret = esp_display_present_pixel_stop(backend->pixel, timeout);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_gram_service(backend); /* Closing: only retire copies, never submit. */
    uint32_t left = present_remaining_ms(start, timeout);
    ret = backend->config.panel.quiesce(backend->config.panel.context, left);
    if (ret != ESP_OK) {
        return ret;
    }
    /* Adapter now guarantees no access/callback. Pending transfers may have
     * been safely aborted; do not falsely report their frames successful. */
    present_lock_enter(&backend->lock, false);
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        gram_slot_t *slot = &backend->slots[i];
        if (slot->state == SLOT_INFLIGHT) {
            frame_record_t *record = &backend->core.frames[slot->frame];
            record_fault(backend, record, slot->cookie, ESP_ERR_INVALID_STATE);
            complete_slot(backend, slot);
        }
    }
    ret = present_frame_core_quiescent(&backend->core);
    present_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_destroy(present_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = present_frame_core_can_destroy(&backend->core);
    if (err != ESP_OK) {
        return err;
    }
    free_backend(backend);
    return ESP_OK;
}

static esp_err_t gram_set_rotation(present_backend_t *backend, esp_display_present_engine_rotation_t rotation)
{
    if (!backend || rotation < ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || rotation > ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->core.life != PRESENT_CORE_RUNNING || backend->core.active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.max_frames; ++i)
        if (backend->core.frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    if (backend->config.in_place && rotation != ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool change_axes = !backend->config.fixed_rotation && (((unsigned)backend->config.rotation ^ (unsigned)rotation) & 1);
    uint32_t width = change_axes ? backend->config.height : backend->config.width;
    uint32_t height = change_axes ? backend->config.width : backend->config.height;
    size_t bpp = backend->config.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    size_t rows = backend->tile_bytes / ((size_t)width * bpp);
    if (!rows) {
        return ESP_ERR_INVALID_SIZE;
    }
    backend->config.width = width;
    backend->config.height = height;
    backend->config.tile_rows = rows > height ? height : rows;
    backend->config.rotation = rotation;
    return ESP_OK;
}

const present_backend_ops_t present_gram_backend = {
    .name = "gram",
    .create = gram_create,
    .get_caps = gram_get_caps,
    .begin = gram_begin,
    .acquire = gram_acquire,
    .get_draw_buffers = gram_get_draw_buffers,
    .submit_region = gram_submit_region,
    .submit_region_ex = gram_submit_region_ex,
    .cancel_region = gram_cancel_region,
    .end = gram_end,
    .end_ex = gram_end_ex,
    .cancel_frame = gram_cancel_frame,
    .query = gram_query,
    .get_fault = gram_get_fault,
    .wait = gram_wait,
    .release_receipt = gram_release_receipt,
    .on_event_isr = gram_on_event_isr,
    .on_event = gram_on_event,
    .stop = gram_stop,
    .destroy = gram_destroy,
    .set_rotation = gram_set_rotation,
};

size_t present_gram_memory(const esp_display_present_backend_config_t *config)
{
    return sizeof(present_backend_t) + config->buffer_count * sizeof(gram_slot_t) + config->max_frames * sizeof(present_core_frame_t) +
           present_pixel_memory(config->buffer_count, config->async_copy);
}
