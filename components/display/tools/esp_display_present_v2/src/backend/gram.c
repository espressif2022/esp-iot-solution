/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "present_backend.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_frame_core.h"
#include "present_transaction.h"
#include "present_allocation.h"

typedef enum { SLOT_FREE, SLOT_LEASED, SLOT_COPYING, SLOT_READY, SLOT_SUBMITTING, SLOT_INFLIGHT } slot_state_t;
typedef pv2_core_frame_t frame_record_t;

typedef struct {
    void *pixels, *converted;
    pv2_allocation_t storage; /* Source/conversion pair is one reusable tile slot. */
    pv2_lease_t lease;
    uint64_t cookie;
    size_t frame;
    slot_state_t state;
    pv2_transaction_t transaction;
    pv2_copy_ticket_t copy;
    pv2_surface_t tx;
    pv2_rect_t area;
} gram_slot_t;

struct pv2_backend {
    pv2_lock_t lock;
    pv2_backend_config_t config;
    pv2_pixel_engine_t *pixel;
    gram_slot_t *slots;
    pv2_frame_core_t core;
    size_t tile_bytes, next_slot;
};

static bool valid_area(const pv2_backend_t *backend, const pv2_rect_t *area)
{
    return pv2_valid_rect(backend->config.width, backend->config.height, area);
}

static frame_record_t *find_frame(pv2_backend_t *backend, uint64_t token)
{
    return pv2_frame_core_find(&backend->core, token);
}

static gram_slot_t *find_lease(pv2_backend_t *backend, uint64_t token, size_t frame)
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
static void PV2_ISR_ATTR record_fault(pv2_backend_t *backend, frame_record_t *record,
                                      uint64_t cookie, esp_err_t error)
{
    pv2_frame_core_fail(&backend->core, record, cookie, error);
}

static void PV2_ISR_ATTR complete_slot(pv2_backend_t *backend, gram_slot_t *slot)
{
    frame_record_t *record = &backend->core.frames[slot->frame];
    if (slot->storage.readers[PV2_REF_COPY]) {
        pv2_allocation_drop(&slot->storage, PV2_REF_COPY);
    }
    pv2_allocation_drop(&slot->storage, PV2_REF_TRANSFER);
    slot->state = SLOT_FREE;
    slot->cookie = 0;
    pv2_frame_core_complete(&backend->core, record);
}

static bool copy_done(void *context, pv2_copy_ticket_t ticket, const pv2_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    pv2_backend_t *backend = context;
    return backend->config.on_progress ? backend->config.on_progress(backend->config.progress_user, isr) : false;
}

/* Lock held; ready copies are not hardware transfers. */
static bool channel_full(const pv2_backend_t *backend)
{
    size_t used = 0, limit = backend->config.transfer_capacity ? backend->config.transfer_capacity : backend->config.buffer_count;
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        used += backend->slots[i].state == SLOT_SUBMITTING || backend->slots[i].state == SLOT_INFLIGHT;
    }
    return used >= limit;
}

esp_err_t pv2_gram_service(pv2_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    for (;;) {
        pv2_lock_enter(&backend->lock, false);
        gram_slot_t *slot = NULL;
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *candidate = &backend->slots[i];
            if ((candidate->state == SLOT_COPYING || candidate->state == SLOT_READY) &&
                    (!slot || candidate->cookie < slot->cookie)) {
                slot = candidate;
            }
        }
        if (!slot) {
            pv2_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        bool copying = slot->state == SLOT_COPYING;
        pv2_lock_exit(&backend->lock, false);
        esp_err_t copy_result = ESP_OK;
        if (copying) {
            pv2_copy_status_t status;
            esp_err_t error = pv2_pixel_wait(backend->pixel, slot->copy, 0, &status);
            if (error == ESP_ERR_TIMEOUT) {
                return ESP_OK;    /* Keep submission order. */
            }
            if (error != ESP_OK) {
                return error;
            }
            copy_result = status.result;
            (void)pv2_pixel_release(backend->pixel, slot->copy);
            slot->copy.value = 0;
        }
        pv2_lock_enter(&backend->lock, false);
        frame_record_t *record = &backend->core.frames[slot->frame];
        if (copying) {
            pv2_allocation_drop(&slot->storage, PV2_REF_COPY);
        }
        record_fault(backend, record, slot->cookie, copy_result);
        if (record->canceled || backend->core.life != PV2_CORE_RUNNING || backend->core.fault.result != ESP_OK) {
            if (!record->canceled && record->common.result == ESP_OK) {
                record_fault(backend, record, slot->cookie, backend->core.fault.result != ESP_OK ? backend->core.fault.result : ESP_ERR_INVALID_STATE);
            }
            complete_slot(backend, slot);
            pv2_lock_exit(&backend->lock, false);
            continue;
        }
        if (channel_full(backend)) {
            slot->state = SLOT_READY;
            pv2_lock_exit(&backend->lock, false);
            return ESP_ERR_NO_MEM;
        }
        slot->state = SLOT_SUBMITTING;
        pv2_transaction_prepare(&slot->transaction);
        pv2_lock_exit(&backend->lock, false);
        pv2_tx_result_t result = backend->config.panel.submit(backend->config.panel.context, &slot->tx, &slot->area, slot->cookie);
        pv2_lock_enter(&backend->lock, false);
        pv2_transaction_resolution_t resolved = pv2_transaction_resolve(&slot->transaction, result);
        if (resolved.rejected) {
            slot->state = SLOT_READY; /* Accepted copy stays owned; retry by service. */
            pv2_lock_exit(&backend->lock, false);
            return result.result == ESP_OK ? ESP_FAIL : result.result;
        }
        record_fault(backend, record, slot->cookie, resolved.result);
        if (resolved.complete) {
            complete_slot(backend, slot);
        } else {
            slot->state = SLOT_INFLIGHT;
        }
        pv2_lock_exit(&backend->lock, false);
    }
}

static bool PV2_ISR_ATTR event_dispatch(void *user, const pv2_backend_event_t *event, bool isr)
{
    pv2_backend_t *backend = user;
    if (!backend || !event || !event->transaction_cookie) {
        return false;
    }
    pv2_lock_enter(&backend->lock, isr);
    if (backend->core.life != PV2_CORE_QUIESCENT) {
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *slot = &backend->slots[i];
            if (!slot->cookie || slot->cookie != event->transaction_cookie ||
                    (slot->state != SLOT_SUBMITTING && slot->state != SLOT_INFLIGHT)) {
                continue;
            }
            frame_record_t *record = &backend->core.frames[slot->frame];
            if (event->kind == PV2_EVENT_FAULT) {
                record_fault(backend, record, slot->cookie, event->result != ESP_OK ? event->result : ESP_FAIL);
                break; /* Fault does NOT establish that hardware stopped. */
            }
            if (event->kind != PV2_EVENT_TRANSFER_DONE) {
                break;
            }
            if (!pv2_transaction_done(&slot->transaction, event->result)) {
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
    pv2_lock_exit(&backend->lock, isr);
    return backend->config.on_progress ? backend->config.on_progress(backend->config.progress_user, isr) : false;
}

static bool PV2_ISR_ATTR gram_on_event_isr(pv2_backend_t *backend, const pv2_backend_event_t *event)
{
    return event_dispatch(backend, event, true);
}

static void gram_on_event(pv2_backend_t *backend, const pv2_backend_event_t *event)
{
    (void)event_dispatch(backend, event, false);
}

static void free_backend(pv2_backend_t *backend)
{
    if (backend->slots) {
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            pv2_free(backend->slots[i].pixels);
            pv2_free(backend->slots[i].converted);
        }
    }
    if (backend->pixel) {
        (void)pv2_pixel_stop(backend->pixel, UINT32_MAX);
        (void)pv2_pixel_destroy(backend->pixel);
    }
    pv2_frame_core_deinit(&backend->core);
    pv2_free(backend->slots);
    pv2_lock_destroy(&backend->lock);
    pv2_free(backend);
}

static esp_err_t gram_create(const pv2_backend_config_t *config, pv2_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (config && config->copy_execute && !config->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config && ((config->internal_tx_staging && !config->async_copy) ||
                   ((config->async_copy || config->rotation != PV2_ROTATE_0) && config->in_place_convert))) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!config || !config->panel.attach || !config->panel.submit || !config->panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->width || !config->height || !config->tile_rows || config->tile_rows > config->height ||
            !config->buffer_count || config->buffer_count > PV2_MAX_TILE_BUFFERS || !config->max_frames || config->max_frames > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->format != PV2_FORMAT_RGB565 && config->format != PV2_FORMAT_RGB888) ||
            (config->draw_order != PV2_ORDER_NATIVE && config->draw_order != PV2_ORDER_SWAPPED) ||
            (config->panel_order != PV2_ORDER_NATIVE && config->panel_order != PV2_ORDER_SWAPPED) ||
            (config->format == PV2_FORMAT_RGB888 && (config->draw_order != PV2_ORDER_NATIVE || config->panel_order != PV2_ORDER_NATIVE))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->render_contract != PV2_CONTRACT_PARTITION || (config->rotation < PV2_ROTATE_0 || config->rotation > PV2_ROTATE_270) || config->initial_scanout_buffer || config->external_buffers || config->external_buffer_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = config->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (config->width > SIZE_MAX / bytes || config->tile_rows > SIZE_MAX / (config->width * bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_backend_t *backend = pv2_alloc(sizeof(*backend));
    if (!backend) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&backend->lock)) {
        pv2_free(backend);
        return ESP_ERR_NO_MEM;
    }
    backend->config = *config;
    backend->core.active = SIZE_MAX;
    backend->tile_bytes = config->width * bytes * config->tile_rows;
    backend->slots = pv2_alloc(config->buffer_count * sizeof(*backend->slots));
    esp_err_t core_error = pv2_frame_core_init(&backend->core, config->max_frames);
    if (!backend->slots || core_error != ESP_OK) {
        free_backend(backend);
        return ESP_ERR_NO_MEM;
    }
    bool convert = config->rotation != PV2_ROTATE_0 || config->async_copy || (config->draw_order != config->panel_order && !config->in_place_convert);
    for (size_t i = 0; i < config->buffer_count; ++i) {
        backend->slots[i].pixels = pv2_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram);
        if (convert) {
            backend->slots[i].converted = pv2_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram && !backend->config.internal_tx_staging);
        }
        if (!backend->slots[i].pixels || (convert && !backend->slots[i].converted)) {
            free_backend(backend);
            return ESP_ERR_NO_MEM;
        }
    }
    pv2_pixel_config_t pixel_config = {.max_jobs = config->buffer_count, .asynchronous = config->async_copy,
                                       .execute = config->copy_execute, .execute_user = config->copy_user,
                                       .on_event = config->async_copy ? copy_done : NULL, .user = backend
                                      };
    esp_err_t ret = pv2_pixel_create(&pixel_config, &backend->pixel);
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

static esp_err_t gram_get_caps(pv2_backend_t *backend, pv2_backend_caps_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    *out = (pv2_backend_caps_t) {
        .contract = PV2_CONTRACT_PARTITION,
        .draw_layout = { backend->config.width, backend->config.height, backend->config.width * bytes,
                         backend->config.format, backend->config.draw_order
                       },
        .drawbuf_capacity_bytes = backend->tile_bytes,
        .buffer_alignment = PV2_BUFFER_ALIGNMENT,
        .max_tile_rows = backend->config.tile_rows, .max_damage_areas = PV2_MAX_DAMAGE_AREAS,
    };
    return ESP_OK;
}

static esp_err_t gram_begin(pv2_backend_t *backend, const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!backend) {
        if (out) {
            out->value = 0;
        }
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = pv2_frame_core_admit(&backend->core, &backend->lock, backend->config.width, backend->config.height, request, out);
    if (ret != ESP_OK) {
        return ret;
    }
    pv2_lock_enter(&backend->lock, false);
    ret = pv2_frame_core_begin(&backend->core, out);
    pv2_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_acquire(pv2_backend_t *backend, pv2_frame_t frame, const pv2_rect_t *area,
                              uint32_t timeout, pv2_lease_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_lease_t) {
        0
    };
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t default_area = { 0, 0, backend->config.width, backend->config.tile_rows };
    const pv2_rect_t *req_area = area ? area : &default_area;
    if (!valid_area(backend, req_area)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        (void)pv2_gram_service(backend);
        pv2_lock_enter(&backend->lock, false);
        frame_record_t *record = find_frame(backend, frame.value);
        if (backend->core.life != PV2_CORE_RUNNING || !record || record->sealed || record->common.result != ESP_OK) {
            pv2_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        for (size_t i = 0; i < backend->config.buffer_count; ++i) {
            gram_slot_t *slot = &backend->slots[(backend->next_slot + i) % backend->config.buffer_count];
            if (slot->state != SLOT_FREE) {
                continue;
            }
            uint64_t token = pv2_new_token();
            if (!token) {
                pv2_lock_exit(&backend->lock, false);
                return ESP_ERR_NO_MEM;
            }
            backend->next_slot = ((size_t)(slot - backend->slots) + 1) % backend->config.buffer_count;
            pv2_rect_t actual = *req_area;
            if (actual.height > backend->config.tile_rows) {
                actual.height = backend->config.tile_rows;
            }
            size_t bytes = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
            slot->lease = (pv2_lease_t) {
                .id = { token }, .logical_area = actual,
                .surface = { slot->pixels, backend->tile_bytes,
                    { actual.width, actual.height, actual.width * bytes, backend->config.format, backend->config.draw_order }
                }
            };
            slot->frame = (size_t)(record - backend->core.frames);
            pv2_allocation_claim(&slot->storage);
            slot->state = SLOT_LEASED;
            ++record->leased;
            *out = slot->lease;
            pv2_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        pv2_lock_exit(&backend->lock, false);
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        if (backend->config.on_progress) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
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

static esp_err_t prepare_pixels(pv2_backend_t *backend, gram_slot_t *slot,
                                const pv2_rect_t *area, size_t src_stride,
                                pv2_surface_t *out, bool *out_swapped_in_place)
{
    size_t bytes = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t tight_stride = area->width * bytes;
    bool need_swap = backend->config.draw_order != backend->config.panel_order;
    bool need_pack = src_stride != tight_stride;
    bool quarter = backend->config.rotation == PV2_ROTATE_90 || backend->config.rotation == PV2_ROTATE_270;
    uint32_t dw = quarter ? area->height : area->width, dh = quarter ? area->width : area->height;
    *out_swapped_in_place = false;
    *out = (pv2_surface_t) {
        .pixels = slot->pixels,
        .capacity_bytes = backend->tile_bytes,
        .layout = { dw, dh, dw * bytes,
                    backend->config.format, backend->config.panel_order
                  },
    };
    if (!need_swap && !need_pack && backend->config.rotation == PV2_ROTATE_0) {
        return ESP_OK;
    }
    if (!need_pack && backend->config.in_place_convert && backend->config.rotation == PV2_ROTATE_0 && bytes == 2) {
        revert_in_place_swap(slot->pixels, (size_t)area->height * tight_stride);
        *out_swapped_in_place = true;
        return ESP_OK;
    }
    if (need_pack || !backend->config.in_place_convert || backend->config.rotation != PV2_ROTATE_0) {
        if (!slot->converted) {
            slot->converted = pv2_alloc_pixels_in(backend->tile_bytes, backend->config.tile_in_psram && !backend->config.internal_tx_staging);
            if (!slot->converted) {
                return ESP_ERR_NO_MEM;
            }
        }
        out->pixels = slot->converted;
    }
    pv2_copy_request_t request = {
        .source = {
            .pixels = slot->pixels,
            .capacity_bytes = backend->tile_bytes,
            .layout = {
                area->width, area->height, src_stride,
                backend->config.format, backend->config.draw_order
            },
        },
        .destination = *out,
        .source_area = { 0, 0, area->width, area->height },
        .destination_x = 0,
        .destination_y = 0,
        .rotation = backend->config.rotation,
    };
    pv2_copy_ticket_t ticket;
    esp_err_t ret = pv2_pixel_submit(backend->pixel, &request, &ticket);
    if (ret != ESP_OK) {
        return ret;
    }
    pv2_copy_status_t status;
    ret = pv2_pixel_wait(backend->pixel, ticket, UINT32_MAX, &status);
    if (ret == ESP_OK) {
        ret = status.result;
    }
    (void)pv2_pixel_release(backend->pixel, ticket);
    if (ret == ESP_OK && out->pixels == slot->pixels && need_swap) {
        *out_swapped_in_place = true;
    }
    return ret;
}

static esp_err_t gram_submit_region_ex(pv2_backend_t *backend, pv2_frame_t frame,
                                       pv2_lease_id_t lease,
                                       const pv2_region_submit_t *sub)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    gram_slot_t *slot = record ? find_lease(backend, lease.value, (size_t)(record - backend->core.frames)) : NULL;
    if (backend->core.life != PV2_CORE_RUNNING || !record || record->sealed || record->common.result != ESP_OK || !slot) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    pv2_rect_t submit_area = slot->lease.logical_area;
    size_t bytes = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
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
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_exit(&backend->lock, false);

    pv2_rect_t physical_area;
    esp_err_t geometry = pv2_rotate_rect(backend->config.width, backend->config.height, backend->config.rotation, &submit_area, &physical_area);
    if (geometry != ESP_OK) {
        return geometry;
    }
    if (backend->config.async_copy) {
        uint64_t cookie = pv2_new_token();
        if (!cookie) {
            return ESP_ERR_NO_MEM;
        }
        pv2_surface_t target = {slot->converted, backend->tile_bytes,
            {physical_area.width, physical_area.height, physical_area.width * bytes, backend->config.format, backend->config.panel_order}
        };
        pv2_copy_request_t request = {
            .source = {
                slot->pixels, backend->tile_bytes,
                {submit_area.width, submit_area.height, src_stride, backend->config.format, backend->config.draw_order}
            },
            .destination = target, .source_area = {0, 0, submit_area.width, submit_area.height}, .rotation = backend->config.rotation
        };
        pv2_copy_ticket_t ticket;
        esp_err_t error = pv2_pixel_submit(backend->pixel, &request, &ticket);
        if (error != ESP_OK) {
            return error;
        }
        /* Task APIs serialized. Callback only wakes, never accesses this slot. */
        pv2_lock_enter(&backend->lock, false);
        slot->copy = ticket;
        slot->cookie = cookie;
        slot->tx = target;
        slot->area = physical_area;
        pv2_allocation_written(&slot->storage);
        pv2_allocation_hold(&slot->storage, PV2_REF_COPY);
        pv2_allocation_hold(&slot->storage, PV2_REF_TRANSFER);
        slot->state = SLOT_COPYING;
        --record->leased;
        ++record->pending;
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }

    pv2_surface_t tx;
    bool swapped_in_place = false;
    esp_err_t ret = prepare_pixels(backend, slot, &submit_area, src_stride, &tx, &swapped_in_place);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t cookie = pv2_new_token();
    if (!cookie) {
        if (swapped_in_place) {
            revert_in_place_swap(slot->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_NO_MEM;
    }
    pv2_lock_enter(&backend->lock, false);
    /* An older transfer may have faulted while the CPU was converting. */
    if (record->common.result != ESP_OK) {
        pv2_lock_exit(&backend->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(slot->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (channel_full(backend)) {
        pv2_lock_exit(&backend->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(slot->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_NO_MEM;
    }
    pv2_allocation_written(&slot->storage);
    pv2_allocation_hold(&slot->storage, PV2_REF_TRANSFER);
    slot->state = SLOT_SUBMITTING;
    slot->cookie = cookie;
    pv2_transaction_prepare(&slot->transaction);
    ++record->pending;
    pv2_lock_exit(&backend->lock, false);

    pv2_tx_result_t result = backend->config.panel.submit(backend->config.panel.context, &tx, &physical_area, cookie);

    pv2_lock_enter(&backend->lock, false);
    pv2_transaction_resolution_t resolved = pv2_transaction_resolve(&slot->transaction, result);
    if (resolved.rejected) {
        --record->pending;
        pv2_allocation_drop(&slot->storage, PV2_REF_TRANSFER);
        pv2_allocation_claim(&slot->storage);
        slot->state = SLOT_LEASED;
        slot->cookie = 0;
        ret = result.result != ESP_OK ? result.result : ESP_FAIL;
        pv2_lock_exit(&backend->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(slot->pixels, submit_area.height * row_bytes);
        }
        return ret;
    }
    /* Early completion proves acceptance even if the adapter returned an
     * inconsistent rejection. Uncertain acceptance never returns a lease. */
    --record->leased;
    record_fault(backend, record, cookie, resolved.result);
    if (resolved.complete) {
        complete_slot(backend, slot);
    } else {
        slot->state = SLOT_INFLIGHT;
    }
    ret = ESP_OK;
    pv2_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_submit_region(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease)
{
    return gram_submit_region_ex(backend, frame, lease, NULL);
}

static esp_err_t gram_cancel_region(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    gram_slot_t *slot = record ? find_lease(backend, lease.value, (size_t)(record - backend->core.frames)) : NULL;
    if (!record || record->sealed || !slot) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    --record->leased;
    pv2_allocation_written(&slot->storage);
    slot->state = SLOT_FREE;
    pv2_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t gram_get_draw_buffers(pv2_backend_t *backend, void **out_buffers,
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

static esp_err_t gram_end_ex(pv2_backend_t *backend, pv2_frame_t frame,
                             const pv2_frame_coverage_t *coverage,
                             pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    if (coverage && pv2_validate_damage(backend->config.width, backend->config.height,
                                        coverage->areas, coverage->area_count) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    frame_record_t *record = find_frame(backend, frame.value);
    esp_err_t ret = pv2_frame_core_seal(&backend->core, record, out);
    pv2_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_end(pv2_backend_t *backend, pv2_frame_t frame, pv2_receipt_t *out)
{
    return gram_end_ex(backend, frame, NULL, out);
}

static void cancel_build(pv2_backend_t *backend, size_t index)
{
    frame_record_t *record = &backend->core.frames[index];
    record->sealed = true;
    record->canceled = true;
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        gram_slot_t *slot = &backend->slots[i];
        if (slot->state == SLOT_LEASED && slot->frame == index) {
            pv2_allocation_written(&slot->storage);
            slot->state = SLOT_FREE;
            --record->leased;
        }
    }
}

static esp_err_t gram_cancel_frame(pv2_backend_t *backend, pv2_frame_t frame, uint32_t timeout)
{
    if (!backend || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        pv2_lock_enter(&backend->lock, false);
        frame_record_t *record = find_frame(backend, frame.value);
        if (!record) {
            if (pv2_cancel_matches(&backend->core.canceled, frame.value)) {
                pv2_lock_exit(&backend->lock, false);
                return ESP_OK;
            }
            pv2_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        if (record->common.external || (record->sealed && !record->canceled)) {
            pv2_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        cancel_build(backend, (size_t)(record - backend->core.frames));
        if (!record->pending) {
            pv2_frame_core_reap(&backend->core, record);
            pv2_lock_exit(&backend->lock, false);
            return ESP_OK;
        }
        pv2_lock_exit(&backend->lock, false);
        (void)pv2_gram_service(backend);
        pv2_lock_enter(&backend->lock, false);
        bool canceled = pv2_cancel_matches(&backend->core.canceled, frame.value);
        pv2_lock_exit(&backend->lock, false);
        if (canceled) {
            return ESP_OK;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static esp_err_t gram_query(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_gram_service(backend);
    return pv2_frame_core_query(&backend->core, &backend->lock, receipt, false, out);
}

static esp_err_t gram_get_fault(pv2_backend_t *backend, pv2_backend_fault_t *out)
{
    return backend ? pv2_frame_core_get_fault(&backend->core, &backend->lock, out) : ESP_ERR_INVALID_ARG;
}

static esp_err_t gram_wait(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_completion_point_t point,
                           uint32_t timeout, pv2_frame_status_t *out)
{
    return pv2_frame_core_wait(backend, gram_query, receipt, point, false, timeout, out);
}

static esp_err_t gram_release_receipt(pv2_backend_t *backend, pv2_receipt_t receipt)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    return pv2_frame_core_release_receipt(&backend->core, &backend->lock, receipt);
}

static esp_err_t gram_stop(pv2_backend_t *backend, uint32_t timeout)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    if (backend->core.life == PV2_CORE_QUIESCENT) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    backend->core.life = PV2_CORE_CLOSING;
    if (backend->core.active != SIZE_MAX) {
        cancel_build(backend, backend->core.active);
    }
    pv2_lock_exit(&backend->lock, false);
    uint64_t start = pv2_now_ms();
    esp_err_t ret = pv2_pixel_stop(backend->pixel, timeout);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_gram_service(backend); /* Closing: only retire copies, never submit. */
    uint32_t left = pv2_remaining_ms(start, timeout);
    ret = backend->config.panel.quiesce(backend->config.panel.context, left);
    if (ret != ESP_OK) {
        return ret;
    }
    /* Adapter now guarantees no access/callback. Pending transfers may have
     * been safely aborted; do not falsely report their frames successful. */
    pv2_lock_enter(&backend->lock, false);
    for (size_t i = 0; i < backend->config.buffer_count; ++i) {
        gram_slot_t *slot = &backend->slots[i];
        if (slot->state == SLOT_INFLIGHT) {
            frame_record_t *record = &backend->core.frames[slot->frame];
            record_fault(backend, record, slot->cookie, ESP_ERR_INVALID_STATE);
            complete_slot(backend, slot);
        }
    }
    ret = pv2_frame_core_quiescent(&backend->core);
    pv2_lock_exit(&backend->lock, false);
    return ret;
}

static esp_err_t gram_destroy(pv2_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = pv2_frame_core_can_destroy(&backend->core);
    if (err != ESP_OK) {
        return err;
    }
    free_backend(backend);
    return ESP_OK;
}

static esp_err_t gram_set_rotation(pv2_backend_t *backend, pv2_rotation_t rotation)
{
    if (!backend || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->core.life != PV2_CORE_RUNNING || backend->core.active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.max_frames; ++i)
        if (backend->core.frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    if (backend->config.in_place_convert && rotation != PV2_ROTATE_0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool change_axes = !backend->config.fixed_logical_rotation && (((unsigned)backend->config.rotation ^ (unsigned)rotation) & 1);
    uint32_t width = change_axes ? backend->config.height : backend->config.width;
    uint32_t height = change_axes ? backend->config.width : backend->config.height;
    size_t bpp = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
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

const pv2_backend_ops_t pv2_gram_backend = {
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

size_t pv2_gram_memory(const pv2_backend_config_t *config)
{
    return sizeof(pv2_backend_t) + config->buffer_count * sizeof(gram_slot_t) + config->max_frames * sizeof(pv2_core_frame_t) +
           pv2_pixel_memory(config->buffer_count, config->async_copy);
}
