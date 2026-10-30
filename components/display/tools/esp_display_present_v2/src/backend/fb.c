/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_records.h"
#include "present_coverage.h"
#include <string.h>

enum { FB_LIMIT = 3, RECORD_LIMIT = PV2_MAX_FRAMES };
typedef enum { FB_SLOT_FREE, FB_SLOT_DRAWING, FB_SLOT_COPY_DRAIN, FB_SLOT_CANCELING, FB_SLOT_READY, FB_SLOT_SUBMITTING, FB_SLOT_PENDING, FB_SLOT_SCANNING } fb_state_t;
typedef struct {
    pv2_surface_t surface;
    pv2_surface_t scan;
    bool draw_owned;
    bool scan_owned;
    fb_state_t state;
    size_t record;
    uint64_t sequence;
    uint64_t cookie;
    bool confirmed;
} fb_slot_t;
typedef pv2_record_t fb_record_t;
struct pv2_backend {
    pv2_lock_t lock;
    pv2_backend_config_t config;
    pv2_pixel_engine_t *pixel;
    fb_slot_t slots[FB_LIMIT];
    fb_record_t records[RECORD_LIMIT];
    size_t active;
    size_t pending;
    size_t scanning;
    size_t baseline;
    size_t bytes;
    size_t tile_bytes;
    size_t coverage_bytes;
    uint8_t *coverage;
    void *scratch;
    pv2_lease_t lease;
    pv2_copy_ticket_t copy;
    pv2_rect_t copy_area;
    bool copy_is_tile;
    size_t repair_pos;
    bool scan_storage;
    bool converted;
    uint64_t sequence;
    pv2_cancel_history_t canceled;
    bool closing;
    bool quiescent;
    bool submitted;
    pv2_backend_fault_t fault;
};
static bool valid_area(pv2_backend_t *b, const pv2_rect_t *a)
{
    return pv2_valid_rect(b->config.width, b->config.height, a);
}

static void PV2_ISR_ATTR fault(pv2_backend_t *b, size_t index, esp_err_t error)
{
    if (error == ESP_OK) {
        return;
    }
    uint64_t token = 0, cookie = 0;
    if (index != SIZE_MAX) {
        fb_slot_t *slot = &b->slots[index];
        fb_record_t *record = &b->records[slot->record];
        token = record->token;
        cookie = slot->cookie;
        pv2_record_fail(record, error);
    }
    pv2_fault_first(&b->fault, token, cookie, error);
}

static bool PV2_ISR_ATTR dispatch(void *context, const pv2_backend_event_t *event, bool isr)
{
    pv2_backend_t *b = context;
    if (!b || !event) {
        return false;
    }
    pv2_lock_enter(&b->lock, isr);
    if (!b->quiescent) {
        if (event->kind == PV2_EVENT_FAULT && !event->transaction_cookie) {
            fault(b, b->pending, event->result == ESP_OK ? ESP_FAIL : event->result);
        } else if (b->pending != SIZE_MAX) {
            fb_slot_t *slot = &b->slots[b->pending];
            if (slot->cookie && slot->cookie == event->transaction_cookie) {
                if (event->kind == PV2_EVENT_SCANOUT_CONFIRMED) {
                    if (event->result == ESP_OK) {
                        slot->confirmed = true;
                    } else {
                        fault(b, b->pending, event->result);
                    }
                } else if (event->kind == PV2_EVENT_FAULT) {
                    fault(b, b->pending, event->result == ESP_OK ? ESP_FAIL : event->result);
                }
            }
        }
    }
    pv2_lock_exit(&b->lock, isr);
    return false;
}

static bool PV2_ISR_ATTR fb_on_event_isr(pv2_backend_t *b, const pv2_backend_event_t *event)
{
    return dispatch(b, event, true);
}

static void fb_on_event(pv2_backend_t *b, const pv2_backend_event_t *event)
{
    (void)dispatch(b, event, false);
}
/* Lock held. Receipt completion is independent of framebuffer retirement. */
static void terminal(pv2_backend_t *b, size_t index)
{
    fb_record_t *record = &b->records[b->slots[index].record];
    record->terminal = true;
    if (!record->external) {
        record->token = 0;
    }
}

static void cover(pv2_backend_t *b, pv2_rect_t area);
static esp_err_t fb_end_ex(pv2_backend_t *, pv2_frame_t, const pv2_frame_coverage_t *, pv2_receipt_t *);
static esp_err_t fb_cancel_frame(pv2_backend_t *, pv2_frame_t, uint32_t);
static bool copy_done(void *context, pv2_copy_ticket_t ticket, const pv2_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    pv2_backend_t *b = context;
    return b->config.on_progress ? b->config.on_progress(b->config.progress_user, isr) : false;
}
/* Owner task only. Scratch and candidate remain pinned through terminal copy. */
static void progress_copy(pv2_backend_t *b)
{
    if (!b->copy.value) {
        return;
    }
    pv2_copy_status_t status;
    if (pv2_pixel_wait(b->pixel, b->copy, 0, &status) != ESP_OK) {
        return;
    }
    (void)pv2_pixel_release(b->pixel, b->copy);
    b->copy.value = 0;
    if (status.result == ESP_OK) {
        if (b->copy_is_tile) {
            cover(b, b->copy_area);
            b->submitted = true;
        }
    } else {
        pv2_lock_enter(&b->lock, false);
        fault(b, b->active, status.result);
        pv2_lock_exit(&b->lock, false);
    }
}

static void finish_build(pv2_backend_t *b)
{
    progress_copy(b);
    if (b->active == SIZE_MAX || b->copy.value || b->closing) {
        return;
    }
    fb_slot_t *slot = &b->slots[b->active];
    fb_record_t *record = &b->records[slot->record];
    pv2_frame_t frame = {record->token};
    if (slot->state == FB_SLOT_CANCELING) {
        slot->state = FB_SLOT_DRAWING;
        (void)fb_cancel_frame(b, frame, 0);
    } else if (slot->state == FB_SLOT_COPY_DRAIN) {
        slot->state = FB_SLOT_DRAWING;
        pv2_lock_enter(&b->lock, false);
        esp_err_t error = b->fault.result;
        pv2_lock_exit(&b->lock, false);
        pv2_receipt_t receipt;
        if (error == ESP_OK) {
            error = fb_end_ex(b, frame, NULL, record->external ? &receipt : NULL);
        }
        if (error != ESP_OK) {
            pv2_lock_enter(&b->lock, false);
            fault(b, b->active, error);
            terminal(b, b->active);
            slot->state = b->active == b->scanning ? FB_SLOT_SCANNING : FB_SLOT_FREE;
            if (b->config.buffer_count == 1) {
                b->baseline = SIZE_MAX;
            }
            b->active = SIZE_MAX;
            pv2_lock_exit(&b->lock, false);
        }
    }
}

esp_err_t pv2_fb_service(pv2_backend_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    finish_build(b);
    pv2_lock_enter(&b->lock, false);
    if (b->pending != SIZE_MAX && b->slots[b->pending].state == FB_SLOT_PENDING && b->slots[b->pending].confirmed) {
        size_t next = b->pending;
        if (b->scanning != SIZE_MAX && b->scanning != next) {
            b->slots[b->scanning].state = FB_SLOT_FREE;
        }
        b->slots[next].state = FB_SLOT_SCANNING;
        b->scanning = next;
        b->pending = SIZE_MAX;
        terminal(b, next);
    }
    if (b->fault.result != ESP_OK) {
        for (size_t i = 0; i < b->config.buffer_count; i++) {
            if (b->slots[i].state == FB_SLOT_READY) {
                fault(b, i, b->fault.result);
                terminal(b, i);
                b->slots[i].state = FB_SLOT_FREE;
            }
        }
    }
    if (b->closing || b->pending != SIZE_MAX || b->fault.result != ESP_OK) {
        pv2_lock_exit(&b->lock, false);
        return ESP_OK;
    }
    size_t next = SIZE_MAX;
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].state == FB_SLOT_READY && (next == SIZE_MAX || b->slots[i].sequence < b->slots[next].sequence)) {
            next = i;
        }
    }
    if (next == SIZE_MAX) {
        pv2_lock_exit(&b->lock, false);
        return ESP_OK;
    }
    fb_slot_t *slot = &b->slots[next];
    slot->cookie = pv2_new_token();
    if (!slot->cookie) {
        pv2_lock_exit(&b->lock, false);
        return ESP_ERR_NO_MEM;
    }
    slot->confirmed = false;
    slot->state = FB_SLOT_SUBMITTING;
    b->pending = next;
    pv2_lock_exit(&b->lock, false);
    const pv2_surface_t *surface = b->scan_storage ? &slot->scan : &slot->surface;
    pv2_rect_t area = {0, 0, surface->layout.width, surface->layout.height};
    pv2_tx_result_t tx = b->config.panel.submit(b->config.panel.context, surface, &area, slot->cookie);
    pv2_lock_enter(&b->lock, false);
    if (tx.acceptance == PV2_TX_REJECTED && !slot->confirmed) {
        slot->state = FB_SLOT_READY;
        slot->cookie = 0;
        b->pending = SIZE_MAX;
        pv2_lock_exit(&b->lock, false);
        return tx.result == ESP_OK ? ESP_FAIL : tx.result;
    }
    if (tx.acceptance != PV2_TX_ACCEPTED || tx.result != ESP_OK) {
        fault(b, next, tx.result == ESP_OK ? ESP_FAIL : tx.result);
    }
    slot->state = FB_SLOT_PENDING;
    /* Early confirmation is consumed by the next pump, not recursively sending. */
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static void free_backend(pv2_backend_t *b)
{
    if (b->pixel) {
        (void)pv2_pixel_stop(b->pixel, UINT32_MAX);
    }
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].draw_owned) {
            pv2_free(b->slots[i].surface.pixels);
        }
        if (b->slots[i].scan_owned) {
            pv2_free(b->slots[i].scan.pixels);
        }
    }
    if (b->pixel) {
        (void)pv2_pixel_stop(b->pixel, 0);
        (void)pv2_pixel_destroy(b->pixel);
    }
    pv2_free(b->scratch);
    pv2_free(b->coverage);
    pv2_lock_destroy(&b->lock);
    pv2_free(b);
}

static esp_err_t fb_create(const pv2_backend_config_t *c, pv2_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (c && c->copy_execute && !c->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!c) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!c->width || !c->height || !c->buffer_count || c->buffer_count > FB_LIMIT ||
            !c->max_frames || c->max_frames > RECORD_LIMIT) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((c->format != PV2_FORMAT_RGB565 && c->format != PV2_FORMAT_RGB888) ||
            (c->draw_order != PV2_ORDER_NATIVE && c->draw_order != PV2_ORDER_SWAPPED) ||
            (c->panel_order != PV2_ORDER_NATIVE && c->panel_order != PV2_ORDER_SWAPPED) ||
            (c->format == PV2_FORMAT_RGB888 && (c->draw_order != PV2_ORDER_NATIVE || c->panel_order != PV2_ORDER_NATIVE))) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((c->rotation < PV2_ROTATE_0 || c->rotation > PV2_ROTATE_270) || c->in_place_convert ||
            c->render_contract > PV2_CONTRACT_DIRECT || c->render_contract < PV2_CONTRACT_PARTITION ||
            (c->buffer_count == 1 && !c->allow_active_scanout_writes) ||
            (c->render_contract != PV2_CONTRACT_PARTITION && c->draw_order != c->panel_order)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->render_contract == PV2_CONTRACT_PARTITION && (!c->tile_rows || c->tile_rows > c->height)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = c->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (c->width > SIZE_MAX / c->height || (size_t)c->width * c->height > (SIZE_MAX - (PV2_BUFFER_ALIGNMENT - 1)) / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t pixels = (size_t)c->width * c->height, bytes = pixels * bpp;
    if ((!c->external_buffers && c->external_buffer_bytes) ||
            (c->external_buffers && c->external_buffer_bytes < bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->external_buffers) {
        for (size_t i = 0; i < c->buffer_count; i++) {
            uintptr_t p = (uintptr_t)c->external_buffers[i];
            if (!p || p % PV2_BUFFER_ALIGNMENT || p > UINTPTR_MAX - c->external_buffer_bytes) {
                return ESP_ERR_INVALID_ARG;
            }
            for (size_t j = 0; j < i; j++) {
                uintptr_t q = (uintptr_t)c->external_buffers[j];
                if (p < q + c->external_buffer_bytes && q < p + c->external_buffer_bytes) {
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
    }
    pv2_backend_t *b = pv2_alloc(sizeof(*b));
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&b->lock)) {
        pv2_free(b);
        return ESP_ERR_NO_MEM;
    }
    b->config = *c;
    b->bytes = bytes;
    b->tile_bytes = c->render_contract == PV2_CONTRACT_PARTITION ? (size_t)c->width * c->tile_rows * bpp : bytes;
    b->active = b->pending = b->scanning = b->baseline = SIZE_MAX;
    b->coverage_bytes = pixels / 8 + (pixels % 8 != 0);
    b->coverage = pv2_alloc(b->coverage_bytes);
    if (c->render_contract == PV2_CONTRACT_PARTITION) {
        b->scratch = pv2_alloc_pixels(b->tile_bytes);
    }
    if (!b->coverage || (c->render_contract == PV2_CONTRACT_PARTITION && !b->scratch)) {
        free_backend(b);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < c->buffer_count; i++) {
        b->slots[i].surface = (pv2_surface_t) {
            c->external_buffers ? c->external_buffers[i] : pv2_alloc_pixels(bytes), bytes,
            {c->width, c->height, c->width * bpp, c->format, c->panel_order}
        };
        b->slots[i].draw_owned = !c->external_buffers;
        if (!b->slots[i].surface.pixels) {
            free_backend(b);
            return ESP_ERR_NO_MEM;
        }
        if (c->rotation != PV2_ROTATE_0) {
            bool quarter = c->rotation == PV2_ROTATE_90 || c->rotation == PV2_ROTATE_270;
            b->slots[i].scan = b->slots[i].surface;
            b->slots[i].scan_owned = b->slots[i].draw_owned;
            b->slots[i].scan.layout.width = quarter ? c->height : c->width;
            b->slots[i].scan.layout.height = quarter ? c->width : c->height;
            b->slots[i].scan.layout.stride_bytes = b->slots[i].scan.layout.width * bpp;
            b->slots[i].surface.pixels = pv2_alloc_pixels(bytes);
            b->slots[i].draw_owned = true;
            if (!b->slots[i].surface.pixels) {
                free_backend(b);
                return ESP_ERR_NO_MEM;
            }
            b->scan_storage = true;
        }
    }
    if (c->initial_scanout_buffer) {
        if (!c->external_buffers) {
            free_backend(b);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < c->buffer_count; ++i)
            if (c->external_buffers[i] == c->initial_scanout_buffer) {
                b->slots[i].state = FB_SLOT_SCANNING;
                b->scanning = i;
            }
        if (b->scanning == SIZE_MAX) {
            free_backend(b);
            return ESP_ERR_INVALID_ARG;
        }
    }
    pv2_pixel_config_t pc = {.max_jobs = 1, .asynchronous = c->async_copy,
                             .execute = c->copy_execute, .execute_user = c->copy_user,
                             .on_event = c->async_copy ? copy_done : NULL, .user = b
                            };
    esp_err_t error = pv2_pixel_create(&pc, &b->pixel);
    if (error == ESP_OK) {
        error = c->panel.attach(c->panel.context, dispatch, b);
    }
    if (error != ESP_OK) {
        free_backend(b);
        return error;
    }
    *out = b;
    return ESP_OK;
}

static esp_err_t fb_get_caps(pv2_backend_t *b, pv2_backend_caps_t *out)
{
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_backend_caps_t) {
        .contract = b->config.render_contract,
        .draw_layout = b->slots[0].surface.layout, .drawbuf_capacity_bytes = b->tile_bytes,
        .buffer_alignment = PV2_BUFFER_ALIGNMENT, .max_damage_areas = PV2_MAX_DAMAGE_AREAS,
        .max_tile_rows = b->config.render_contract == PV2_CONTRACT_PARTITION ? b->config.tile_rows : b->config.height,
        .retains_content = true, .supports_present_confirmation = true,
        .writes_active_scanout = b->config.buffer_count == 1
    };
    out->draw_layout.byte_order = b->config.draw_order;
    return ESP_OK;
}

static esp_err_t validate_damage(pv2_backend_t *b, const pv2_rect_t *areas, size_t count)
{
    return pv2_validate_damage(b->config.width, b->config.height, areas, count);
}

static esp_err_t fb_begin(pv2_backend_t *b, const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!b || !request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = validate_damage(b, request->damage, request->damage_count);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_fb_service(b);
    if (b->active != SIZE_MAX) {
        return b->slots[b->active].state == FB_SLOT_COPY_DRAIN ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
    }
    pv2_lock_enter(&b->lock, false);
    bool unavailable = b->closing || b->fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (unavailable) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t slot = SIZE_MAX, record = SIZE_MAX;
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        if (b->slots[i].state == FB_SLOT_FREE || (b->config.buffer_count == 1 && b->slots[i].state == FB_SLOT_SCANNING)) {
            slot = i;
            break;
        }
    }
    for (size_t i = 0; i < b->config.max_frames; i++) {
        if (!b->records[i].token) {
            record = i;
            break;
        }
    }
    if (slot == SIZE_MAX || record == SIZE_MAX) {
        return ESP_ERR_NO_MEM;
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    /* Prepopulate a stable latest sealed frame. Sealed buffers are immutable;
     * single-producer pump cannot retire/reuse it during this CPU copy. */
    bool prefill = b->baseline != SIZE_MAX && b->baseline != slot && b->config.render_contract != PV2_CONTRACT_FULL;
    if (prefill && !b->config.async_copy) {
        memcpy(b->slots[slot].surface.pixels, b->slots[b->baseline].surface.pixels, b->bytes);
    }
    memset(b->coverage, 0, b->coverage_bytes);
    pv2_lock_enter(&b->lock, false);
    b->active = slot;
    b->slots[slot].state = FB_SLOT_DRAWING;
    b->slots[slot].record = record;
    b->records[record] = (fb_record_t) {
        .token = token
    };
    pv2_lock_exit(&b->lock, false);
    b->submitted = false;
    b->repair_pos = 0;
    b->converted = false;
    if (prefill && b->config.async_copy) {
        pv2_surface_t source = b->slots[b->baseline].surface;
        pv2_copy_request_t copy = {.source = {source.pixels, source.capacity_bytes, source.layout},
                                   .destination = b->slots[slot].surface, .source_area = {0, 0, b->config.width, b->config.height}
                                  };
        error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
        if (error != ESP_OK) {
            b->records[record].token = 0;
            b->slots[slot].state = FB_SLOT_FREE;
            b->active = SIZE_MAX;
            return error;
        }
        b->copy_is_tile = false;
    }
    out->value = token;
    return ESP_OK;
}

static bool building(pv2_backend_t *b, pv2_frame_t frame)
{
    return b && !b->closing && b->active != SIZE_MAX && b->slots[b->active].state == FB_SLOT_DRAWING && frame.value &&
           b->records[b->slots[b->active].record].token == frame.value;
}

static esp_err_t fb_acquire(pv2_backend_t *b, pv2_frame_t frame, const pv2_rect_t *area,
                            uint32_t timeout, pv2_lease_t *out)
{
    (void)timeout;
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_lease_t) {
        0
    };
    if (!building(b, frame)) {
        return ESP_ERR_INVALID_STATE;
    }
    progress_copy(b);
    pv2_lock_enter(&b->lock, false);
    bool failed = b->fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (failed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (b->lease.id.value || b->copy.value) {
        return ESP_ERR_TIMEOUT;
    }
    pv2_rect_t actual = {0, 0, b->config.width, b->config.height};
    pv2_surface_t surface = b->slots[b->active].surface;
    if (area && !valid_area(b, area)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        actual = area ? *area : (pv2_rect_t) {
            0, 0, b->config.width, b->config.tile_rows
        };
        if (actual.height > b->config.tile_rows) {
            actual.height = b->config.tile_rows;
        }
        size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
        surface = (pv2_surface_t) {
            b->scratch, b->tile_bytes,
            {actual.width, actual.height, actual.width * bpp, b->config.format, b->config.draw_order}
        };
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    b->lease = (pv2_lease_t) {
        .id = {token}, .surface = surface, .logical_area = actual
    };
    *out = b->lease;
    return ESP_OK;
}

static void cover(pv2_backend_t *b, pv2_rect_t area)
{
    for (uint32_t y = area.y; y < area.y + area.height; y++) {
        pv2_coverage_set(b->coverage, (size_t)y * b->config.width + area.x, area.width);
    }
}

static bool is_covered(pv2_backend_t *b, size_t pixel)
{
    return (b->coverage[pixel / 8] & (1u << (pixel % 8))) != 0;
}

static esp_err_t fb_submit_region_ex(pv2_backend_t *b, pv2_frame_t frame,
                                     pv2_lease_id_t lease, const pv2_region_submit_t *submit)
{
    if (!building(b, frame) || !lease.value || lease.value != b->lease.id.value) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_lock_enter(&b->lock, false);
    bool failed = b->fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (failed) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_rect_t area = b->lease.logical_area;
    if (submit && (submit->logical_area.x || submit->logical_area.y || submit->logical_area.width || submit->logical_area.height)) {
        area = submit->logical_area;
    }
    if (!valid_area(b, &area)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (b->config.render_contract == PV2_CONTRACT_PARTITION) {
        size_t stride = submit && submit->stride_bytes ? submit->stride_bytes : area.width * bpp;
        pv2_copy_request_t copy = {
            .source = {b->scratch, b->tile_bytes, {area.width, area.height, stride, b->config.format, b->config.draw_order}},
            .source_area = {0, 0, area.width, area.height}, .destination = b->slots[b->active].surface,
            .destination_x = area.x, .destination_y = area.y
        };
        pv2_copy_ticket_t ticket;
        esp_err_t error = pv2_pixel_submit(b->pixel, &copy, &ticket);
        if (error != ESP_OK) {
            return error;
        }
        if (b->config.async_copy) {
            b->copy = ticket;
            b->copy_area = area;
            b->copy_is_tile = true;
            b->lease.id.value = 0;
            return ESP_OK;
        }
        pv2_copy_status_t status;
        error = pv2_pixel_wait(b->pixel, ticket, UINT32_MAX, &status);
        (void)pv2_pixel_release(b->pixel, ticket);
        if (error != ESP_OK || status.result != ESP_OK) {
            return error != ESP_OK ? error : status.result;
        }
    } else {
        if (submit && submit->stride_bytes && submit->stride_bytes != b->lease.surface.layout.stride_bytes) {
            return ESP_ERR_INVALID_ARG;
        }
        if (b->config.render_contract == PV2_CONTRACT_FULL &&
                (area.x || area.y || area.width != b->config.width || area.height != b->config.height)) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    cover(b, area);
    b->submitted = true;
    b->lease.id.value = 0;
    return ESP_OK;
}

static esp_err_t fb_submit_region(pv2_backend_t *b, pv2_frame_t frame, pv2_lease_id_t lease)
{
    return fb_submit_region_ex(b, frame, lease, NULL);
}

static esp_err_t fb_cancel_region(pv2_backend_t *b, pv2_frame_t frame, pv2_lease_id_t lease)
{
    if (!building(b, frame) || !lease.value || lease.value != b->lease.id.value) {
        return ESP_ERR_INVALID_STATE;
    }
    b->lease.id.value = 0;
    return ESP_OK;
}

static esp_err_t fb_get_draw_buffers(pv2_backend_t *b, void **out, size_t capacity, size_t *count, size_t *bytes)
{
    if (!b || !count || (capacity && !out)) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = b->config.render_contract == PV2_CONTRACT_PARTITION ? 1 : b->config.buffer_count;
    if (bytes) {
        *bytes = b->tile_bytes;
    }
    if (capacity < *count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < *count; i++) {
        out[i] = b->config.render_contract == PV2_CONTRACT_PARTITION ? b->scratch : b->slots[i].surface.pixels;
    }
    return ESP_OK;
}

static esp_err_t fb_end_ex(pv2_backend_t *b, pv2_frame_t frame,
                           const pv2_frame_coverage_t *coverage, pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!building(b, frame) || b->lease.id.value) {
        return ESP_ERR_INVALID_STATE;
    }
    if (coverage) {
        esp_err_t error = validate_damage(b, coverage->areas, coverage->area_count);
        if (error != ESP_OK) {
            return error;
        }
    }
    progress_copy(b);
    if (b->copy.value) {
        b->slots[b->active].state = FB_SLOT_COPY_DRAIN;
        b->records[b->slots[b->active].record].external = out != NULL;
        if (out) {
            out->value = frame.value;
        }
        return ESP_OK;
    }
    pv2_lock_enter(&b->lock, false);
    bool failed = b->fault.result != ESP_OK;
    pv2_lock_exit(&b->lock, false);
    if (failed) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Coverage is proven by submitted regions, never by unverified end hints. */
    bool require_full = b->baseline == SIZE_MAX || b->config.render_contract == PV2_CONTRACT_FULL;
    size_t pixels = (size_t)b->config.width * b->config.height;
    if (require_full) {
        if (!pv2_coverage_full(b->coverage, pixels)) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (!b->submitted) {
        return ESP_ERR_INVALID_STATE;
    }
    /* DIRECT permits writes before submit. Restore undeclared pixels from the
     * immutable baseline (multi-buffer only); canceled leases grant no coverage. */
    if (b->config.render_contract == PV2_CONTRACT_DIRECT && b->baseline != SIZE_MAX && b->baseline != b->active) {
        size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
        uint8_t *dst = b->slots[b->active].surface.pixels;
        const uint8_t *src = b->slots[b->baseline].surface.pixels;
        while (b->repair_pos < pixels) {
            size_t i = b->repair_pos;
            if (is_covered(b, i)) {
                b->repair_pos++;
                continue;
            }
            uint32_t x = i % b->config.width, y = i / b->config.width, end = x + 1;
            while (end < b->config.width && !is_covered(b, (size_t)y * b->config.width + end)) {
                end++;
            }
            if (!b->config.async_copy) {
                memcpy(dst + i * bpp, src + i * bpp, (end - x) * bpp);
                b->repair_pos += end - x;
                continue;
            }
            pv2_surface_t base = b->slots[b->baseline].surface;
            pv2_copy_request_t copy = {.source = {base.pixels, base.capacity_bytes, base.layout},
                                       .destination = b->slots[b->active].surface, .source_area = {x, y, end - x, 1},
                                       .destination_x = x, .destination_y = y
                                      };
            esp_err_t error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
            if (error != ESP_OK) {
                return error;
            }
            b->copy_is_tile = false;
            b->repair_pos += end - x;
            b->slots[b->active].state = FB_SLOT_COPY_DRAIN;
            b->records[b->slots[b->active].record].external = out != NULL;
            if (out) {
                out->value = frame.value;
            }
            return ESP_OK;
        }
    }
    if (b->scan_storage && !b->converted) {
        pv2_surface_t source = b->slots[b->active].surface;
        pv2_copy_request_t copy = {.source = {source.pixels, source.capacity_bytes, source.layout},
                                   .destination = b->slots[b->active].scan,
                                   .source_area = {0, 0, b->config.width, b->config.height}, .rotation = b->config.rotation
                                  };
        esp_err_t error = pv2_pixel_submit(b->pixel, &copy, &b->copy);
        if (error != ESP_OK) {
            return error;
        }
        b->copy_is_tile = false;
        b->converted = true;
        if (b->config.async_copy) {
            b->slots[b->active].state = FB_SLOT_COPY_DRAIN;
            b->records[b->slots[b->active].record].external = out != NULL;
            if (out) {
                out->value = frame.value;
            }
            return ESP_OK;
        }
        progress_copy(b);
        pv2_lock_enter(&b->lock, false);
        error = b->fault.result;
        pv2_lock_exit(&b->lock, false);
        if (error != ESP_OK) {
            return error;
        }
    }
    if (b->sequence == UINT64_MAX) {
        return ESP_ERR_NO_MEM;
    }
    pv2_lock_enter(&b->lock, false);
    fb_slot_t *slot = &b->slots[b->active];
    slot->sequence = ++b->sequence;
    slot->state = FB_SLOT_READY;
    fb_record_t *record = &b->records[slot->record];
    record->external = out != NULL;
    if (out) {
        out->value = frame.value;
    }
    b->baseline = b->active;
    b->active = SIZE_MAX;
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static esp_err_t fb_end(pv2_backend_t *b, pv2_frame_t frame, pv2_receipt_t *out)
{
    return fb_end_ex(b, frame, NULL, out);
}

static esp_err_t fb_cancel_frame(pv2_backend_t *b, pv2_frame_t frame, uint32_t timeout)
{
    if (!b || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pv2_cancel_matches(&b->canceled, frame.value)) {
        return ESP_OK;
    }
    if (b->active != SIZE_MAX && b->slots[b->active].state == FB_SLOT_CANCELING &&
            b->records[b->slots[b->active].record].token == frame.value) {
        /* Retry the same cancellation, without reopening drawing. */
    } else if (!building(b, frame)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    while (b->copy.value) {
        b->slots[b->active].state = FB_SLOT_CANCELING;
        b->lease.id.value = 0;
        progress_copy(b);
        if (!b->copy.value) {
            break;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
    pv2_lock_enter(&b->lock, false);
    size_t index = b->active;
    b->records[b->slots[index].record].token = 0;
    b->slots[index].state = index == b->scanning ? FB_SLOT_SCANNING : FB_SLOT_FREE;
    if (b->config.buffer_count == 1) {
        b->baseline = SIZE_MAX;
    }
    b->lease.id.value = 0;
    b->active = SIZE_MAX;
    pv2_cancel_publish(&b->canceled, frame.value);
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static fb_record_t *find_record(pv2_backend_t *b, pv2_receipt_t receipt)
{
    return pv2_record_find(b->records, b->config.max_frames, sizeof(*b->records), receipt.value, true);
}

static esp_err_t fb_query(pv2_backend_t *b, pv2_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_fb_service(b);
    pv2_lock_enter(&b->lock, false);
    fb_record_t *record = find_record(b, receipt);
    if (!record) {
        pv2_lock_exit(&b->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    *out = pv2_record_status(record, true, record->terminal, record->terminal, record->terminal && record->result == ESP_OK);
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static esp_err_t fb_get_fault(pv2_backend_t *b, pv2_backend_fault_t *out)
{
    if (!b || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&b->lock, false);
    *out = b->fault;
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static esp_err_t fb_wait(pv2_backend_t *b, pv2_receipt_t receipt, pv2_completion_point_t point,
                         uint32_t timeout, pv2_frame_status_t *out)
{
    if (point != PV2_COMPLETION_SOURCE_RELEASED && point != PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        esp_err_t error = fb_query(b, receipt, out);
        if (error != ESP_OK || out->terminal) {
            return error;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static esp_err_t fb_release_receipt(pv2_backend_t *b, pv2_receipt_t receipt)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_fb_service(b);
    pv2_lock_enter(&b->lock, false);
    fb_record_t *record = find_record(b, receipt);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (record && record->terminal) {
        record->token = 0;
        error = ESP_OK;
    }
    pv2_lock_exit(&b->lock, false);
    return error;
}

static esp_err_t fb_stop(pv2_backend_t *b, uint32_t timeout)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    if (b->quiescent) {
        return ESP_OK;
    }
    b->closing = true;
    uint64_t start = pv2_now_ms();
    esp_err_t error = pv2_pixel_stop(b->pixel, timeout);
    if (error != ESP_OK) {
        return error;
    }
    progress_copy(b);
    uint32_t left = pv2_remaining_ms(start, timeout);
    error = b->config.panel.quiesce(b->config.panel.context, left);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_fb_service(b);
    pv2_lock_enter(&b->lock, false);
    for (size_t i = 0; i < b->config.buffer_count; i++) {
        fb_slot_t *slot = &b->slots[i];
        if (slot->state != FB_SLOT_FREE && slot->state != FB_SLOT_SCANNING) {
            fault(b, i, ESP_ERR_INVALID_STATE);
            terminal(b, i);
        }
        slot->state = FB_SLOT_FREE;
    }
    b->active = b->pending = b->scanning = b->baseline = SIZE_MAX;
    b->lease.id.value = 0;
    b->quiescent = true;
    pv2_lock_exit(&b->lock, false);
    return ESP_OK;
}

static esp_err_t fb_destroy(pv2_backend_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!b->quiescent) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < b->config.max_frames; i++) {
        if (b->records[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    free_backend(b);
    return ESP_OK;
}

static esp_err_t fb_set_rotation(pv2_backend_t *b, pv2_rotation_t rotation)
{
    if (!b || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (b->closing || b->active != SIZE_MAX || b->pending != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < b->config.max_frames; ++i)
        if (b->records[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    if (rotation == b->config.rotation) {
        return ESP_OK;
    }
    bool axes = ((unsigned)rotation ^ (unsigned)b->config.rotation) & 1;
    uint32_t width = axes ? b->config.height : b->config.width;
    uint32_t height = axes ? b->config.width : b->config.height;
    size_t bpp = b->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t rows = b->tile_bytes / ((size_t)width * bpp);
    if (!rows) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (rotation == PV2_ROTATE_0 && b->scan_storage) {
        /* Drained receipts do not retire current scanout. Preserve the physical
         * pool and each slot's FB_SLOT_SCANNING/FB_SLOT_FREE state; free only logical storage. */
        for (size_t i = 0; i < b->config.buffer_count; i++) {
            if (b->slots[i].draw_owned) {
                pv2_free(b->slots[i].surface.pixels);
            }
            b->slots[i].surface = b->slots[i].scan;
            b->slots[i].draw_owned = b->slots[i].scan_owned;
            b->slots[i].scan = (pv2_surface_t) {
                0
            };
            b->slots[i].scan_owned = false;
        }
        b->scan_storage = false;
    } else if (!b->scan_storage) {
        void *draw[FB_LIMIT] = {0};
        for (size_t i = 0; i < b->config.buffer_count; ++i) {
            draw[i] = pv2_alloc_pixels(b->bytes);
            if (!draw[i]) {
                for (size_t j = 0; j < i; ++j) {
                    pv2_free(draw[j]);
                }
                return ESP_ERR_NO_MEM;
            }
        }
        /* Preserve every existing physical scan pointer, including current. */
        for (size_t i = 0; i < b->config.buffer_count; ++i) {
            b->slots[i].scan = b->slots[i].surface;
            b->slots[i].scan_owned = b->slots[i].draw_owned;
            b->slots[i].surface.pixels = draw[i];
            b->slots[i].draw_owned = true;
        }
        b->scan_storage = true;
    }
    for (size_t i = 0; i < b->config.buffer_count; ++i) {
        b->slots[i].surface.layout.width = width;
        b->slots[i].surface.layout.height = height;
        b->slots[i].surface.layout.stride_bytes = width * bpp;
    }
    b->config.width = width;
    b->config.height = height;
    b->config.rotation = rotation;
    b->config.tile_rows = rows > height ? height : rows;
    b->baseline = SIZE_MAX; /* Logical orientation changed: require full redraw. */
    return ESP_OK;
}

const pv2_backend_ops_t pv2_fb_backend = {
    .name = "fb",
    .create = fb_create,
    .get_caps = fb_get_caps,
    .begin = fb_begin,
    .acquire = fb_acquire,
    .get_draw_buffers = fb_get_draw_buffers,
    .submit_region = fb_submit_region,
    .submit_region_ex = fb_submit_region_ex,
    .cancel_region = fb_cancel_region,
    .end = fb_end,
    .end_ex = fb_end_ex,
    .cancel_frame = fb_cancel_frame,
    .query = fb_query,
    .get_fault = fb_get_fault,
    .wait = fb_wait,
    .release_receipt = fb_release_receipt,
    .on_event_isr = fb_on_event_isr,
    .on_event = fb_on_event,
    .stop = fb_stop,
    .destroy = fb_destroy,
    .set_rotation = fb_set_rotation,
};
