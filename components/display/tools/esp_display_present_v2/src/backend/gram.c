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
    size_t tile_bytes;
};

static bool valid_area(const pv2_backend_t *g, const pv2_rect_t *a)
{
    return pv2_valid_rect(g->config.width, g->config.height, a);
}

static frame_record_t *find_frame(pv2_backend_t *g, uint64_t token)
{
    return pv2_frame_core_find(&g->core, token);
}

static gram_slot_t *find_lease(pv2_backend_t *g, uint64_t token, size_t frame)
{
    if (!token) {
        return NULL;
    }
    for (size_t i = 0; i < g->config.buffer_count; ++i) {
        gram_slot_t *s = &g->slots[i];
        if (s->state == SLOT_LEASED && s->lease.id.value == token && s->frame == frame) {
            return s;
        }
    }
    return NULL;
}

/* Called under backend lock; preserve first failure even after auto-recycle. */
static void PV2_ISR_ATTR record_fault(pv2_backend_t *g, frame_record_t *f,
                                      uint64_t cookie, esp_err_t error)
{
    pv2_frame_core_fail(&g->core, f, cookie, error);
}

static void PV2_ISR_ATTR complete_slot(pv2_backend_t *g, gram_slot_t *s)
{
    frame_record_t *f = &g->core.frames[s->frame];
    if (s->storage.readers[PV2_REF_COPY]) {
        pv2_allocation_drop(&s->storage, PV2_REF_COPY);
    }
    pv2_allocation_drop(&s->storage, PV2_REF_TRANSFER);
    s->state = SLOT_FREE;
    s->cookie = 0;
    pv2_frame_core_complete(&g->core, f);
}

static bool copy_done(void *context, pv2_copy_ticket_t ticket, const pv2_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    pv2_backend_t *g = context;
    return g->config.on_progress ? g->config.on_progress(g->config.progress_user, isr) : false;
}

/* Lock held; ready copies are not hardware transfers. */
static bool channel_full(const pv2_backend_t *g)
{
    size_t used = 0, limit = g->config.transfer_capacity ? g->config.transfer_capacity : g->config.buffer_count;
    for (size_t i = 0; i < g->config.buffer_count; i++) {
        used += g->slots[i].state == SLOT_SUBMITTING || g->slots[i].state == SLOT_INFLIGHT;
    }
    return used >= limit;
}

esp_err_t pv2_gram_service(pv2_backend_t *g)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    for (;;) {
        pv2_lock_enter(&g->lock, false);
        gram_slot_t *s = NULL;
        for (size_t i = 0; i < g->config.buffer_count; ++i) {
            gram_slot_t *candidate = &g->slots[i];
            if ((candidate->state == SLOT_COPYING || candidate->state == SLOT_READY) &&
                    (!s || candidate->cookie < s->cookie)) {
                s = candidate;
            }
        }
        if (!s) {
            pv2_lock_exit(&g->lock, false);
            return ESP_OK;
        }
        bool copying = s->state == SLOT_COPYING;
        pv2_lock_exit(&g->lock, false);
        esp_err_t copy_result = ESP_OK;
        if (copying) {
            pv2_copy_status_t status;
            esp_err_t error = pv2_pixel_wait(g->pixel, s->copy, 0, &status);
            if (error == ESP_ERR_TIMEOUT) {
                return ESP_OK;    /* Keep submission order. */
            }
            if (error != ESP_OK) {
                return error;
            }
            copy_result = status.result;
            (void)pv2_pixel_release(g->pixel, s->copy);
            s->copy.value = 0;
        }
        pv2_lock_enter(&g->lock, false);
        frame_record_t *f = &g->core.frames[s->frame];
        if (copying) {
            pv2_allocation_drop(&s->storage, PV2_REF_COPY);
        }
        record_fault(g, f, s->cookie, copy_result);
        if (f->canceled || g->core.life != PV2_CORE_RUNNING || g->core.fault.result != ESP_OK) {
            if (!f->canceled && f->common.result == ESP_OK) {
                record_fault(g, f, s->cookie, g->core.fault.result != ESP_OK ? g->core.fault.result : ESP_ERR_INVALID_STATE);
            }
            complete_slot(g, s);
            pv2_lock_exit(&g->lock, false);
            continue;
        }
        if (channel_full(g)) {
            s->state = SLOT_READY;
            pv2_lock_exit(&g->lock, false);
            return ESP_ERR_NO_MEM;
        }
        s->state = SLOT_SUBMITTING;
        pv2_transaction_prepare(&s->transaction);
        pv2_lock_exit(&g->lock, false);
        pv2_tx_result_t result = g->config.panel.submit(g->config.panel.context, &s->tx, &s->area, s->cookie);
        pv2_lock_enter(&g->lock, false);
        pv2_transaction_resolution_t resolved = pv2_transaction_resolve(&s->transaction, result);
        if (resolved.rejected) {
            s->state = SLOT_READY; /* Accepted copy stays owned; retry by service. */
            pv2_lock_exit(&g->lock, false);
            return result.result == ESP_OK ? ESP_FAIL : result.result;
        }
        record_fault(g, f, s->cookie, resolved.result);
        if (resolved.complete) {
            complete_slot(g, s);
        } else {
            s->state = SLOT_INFLIGHT;
        }
        pv2_lock_exit(&g->lock, false);
    }
}

static bool PV2_ISR_ATTR event_dispatch(void *user, const pv2_backend_event_t *event, bool isr)
{
    pv2_backend_t *g = user;
    if (!g || !event || !event->transaction_cookie) {
        return false;
    }
    pv2_lock_enter(&g->lock, isr);
    if (g->core.life != PV2_CORE_QUIESCENT) {
        for (size_t i = 0; i < g->config.buffer_count; ++i) {
            gram_slot_t *s = &g->slots[i];
            if (!s->cookie || s->cookie != event->transaction_cookie ||
                    (s->state != SLOT_SUBMITTING && s->state != SLOT_INFLIGHT)) {
                continue;
            }
            frame_record_t *f = &g->core.frames[s->frame];
            if (event->kind == PV2_EVENT_FAULT) {
                record_fault(g, f, s->cookie, event->result != ESP_OK ? event->result : ESP_FAIL);
                break; /* Fault does NOT establish that hardware stopped. */
            }
            if (event->kind != PV2_EVENT_TRANSFER_DONE) {
                break;
            }
            if (!pv2_transaction_done(&s->transaction, event->result)) {
                record_fault(g, f, s->cookie, ESP_ERR_INVALID_STATE);
                break;
            }
            record_fault(g, f, s->cookie, event->result);
            if (s->state == SLOT_INFLIGHT) {
                complete_slot(g, s);
            }
            break;
        }
    }
    pv2_lock_exit(&g->lock, isr);
    return false; /* Initial implementation uses task-side bounded polling. */
}

static bool PV2_ISR_ATTR gram_on_event_isr(pv2_backend_t *g, const pv2_backend_event_t *event)
{
    return event_dispatch(g, event, true);
}

static void gram_on_event(pv2_backend_t *g, const pv2_backend_event_t *event)
{
    (void)event_dispatch(g, event, false);
}

static void free_backend(pv2_backend_t *g)
{
    if (g->slots) {
        for (size_t i = 0; i < g->config.buffer_count; ++i) {
            pv2_free(g->slots[i].pixels);
            pv2_free(g->slots[i].converted);
        }
    }
    if (g->pixel) {
        (void)pv2_pixel_stop(g->pixel, UINT32_MAX);
        (void)pv2_pixel_destroy(g->pixel);
    }
    pv2_frame_core_deinit(&g->core);
    pv2_free(g->slots);
    pv2_lock_destroy(&g->lock);
    pv2_free(g);
}

static esp_err_t gram_create(const pv2_backend_config_t *c, pv2_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (c && c->copy_execute && !c->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c && (c->async_copy || c->rotation != PV2_ROTATE_0) && c->in_place_convert) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!c || !c->panel.attach || !c->panel.submit || !c->panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!c->width || !c->height || !c->tile_rows || c->tile_rows > c->height ||
            !c->buffer_count || c->buffer_count > PV2_MAX_TILE_BUFFERS || !c->max_frames || c->max_frames > PV2_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((c->format != PV2_FORMAT_RGB565 && c->format != PV2_FORMAT_RGB888) ||
            (c->draw_order != PV2_ORDER_NATIVE && c->draw_order != PV2_ORDER_SWAPPED) ||
            (c->panel_order != PV2_ORDER_NATIVE && c->panel_order != PV2_ORDER_SWAPPED) ||
            (c->format == PV2_FORMAT_RGB888 && (c->draw_order != PV2_ORDER_NATIVE || c->panel_order != PV2_ORDER_NATIVE))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->render_contract != PV2_CONTRACT_PARTITION || (c->rotation < PV2_ROTATE_0 || c->rotation > PV2_ROTATE_270) || c->initial_scanout_buffer || c->external_buffers || c->external_buffer_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = c->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (c->width > SIZE_MAX / bytes || c->tile_rows > SIZE_MAX / (c->width * bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_backend_t *g = pv2_alloc(sizeof(*g));
    if (!g) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&g->lock)) {
        pv2_free(g);
        return ESP_ERR_NO_MEM;
    }
    g->config = *c;
    g->core.active = SIZE_MAX;
    g->tile_bytes = c->width * bytes * c->tile_rows;
    g->slots = pv2_alloc(c->buffer_count * sizeof(*g->slots));
    esp_err_t core_error = pv2_frame_core_init(&g->core, c->max_frames);
    if (!g->slots || core_error != ESP_OK) {
        free_backend(g);
        return ESP_ERR_NO_MEM;
    }
    bool convert = c->rotation != PV2_ROTATE_0 || c->async_copy || (c->draw_order != c->panel_order && !c->in_place_convert);
    for (size_t i = 0; i < c->buffer_count; ++i) {
        g->slots[i].pixels = pv2_alloc_pixels_in(g->tile_bytes, g->config.tile_in_psram);
        if (convert) {
            g->slots[i].converted = pv2_alloc_pixels_in(g->tile_bytes, g->config.tile_in_psram);
        }
        if (!g->slots[i].pixels || (convert && !g->slots[i].converted)) {
            free_backend(g);
            return ESP_ERR_NO_MEM;
        }
    }
    pv2_pixel_config_t pixel_config = {.max_jobs = c->buffer_count, .asynchronous = c->async_copy,
                                       .execute = c->copy_execute, .execute_user = c->copy_user,
                                       .on_event = c->async_copy ? copy_done : NULL, .user = g
                                      };
    esp_err_t ret = pv2_pixel_create(&pixel_config, &g->pixel);
    if (ret != ESP_OK) {
        free_backend(g);
        return ret;
    }
    ret = c->panel.attach(c->panel.context, event_dispatch, g);
    if (ret != ESP_OK) {
        free_backend(g);
        return ret;
    }
    *out = g;
    return ESP_OK;
}

static esp_err_t gram_get_caps(pv2_backend_t *g, pv2_backend_caps_t *out)
{
    if (!g || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bytes = g->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    *out = (pv2_backend_caps_t) {
        .contract = PV2_CONTRACT_PARTITION,
        .draw_layout = { g->config.width, g->config.height, g->config.width * bytes,
                         g->config.format, g->config.draw_order
                       },
        .drawbuf_capacity_bytes = g->tile_bytes,
        .buffer_alignment = PV2_BUFFER_ALIGNMENT,
        .max_tile_rows = g->config.tile_rows, .max_damage_areas = PV2_MAX_DAMAGE_AREAS,
    };
    return ESP_OK;
}

static esp_err_t gram_begin(pv2_backend_t *g, const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!g || !request || pv2_validate_damage(g->config.width, g->config.height,
                                              request->damage, request->damage_count) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    esp_err_t ret = pv2_frame_core_begin(&g->core, out);
    pv2_lock_exit(&g->lock, false);
    return ret;
}

static esp_err_t gram_acquire(pv2_backend_t *g, pv2_frame_t frame, const pv2_rect_t *area,
                              uint32_t timeout, pv2_lease_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_lease_t) {
        0
    };
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t default_area = { 0, 0, g->config.width, g->config.tile_rows };
    const pv2_rect_t *req_area = area ? area : &default_area;
    if (!valid_area(g, req_area)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        (void)pv2_gram_service(g);
        pv2_lock_enter(&g->lock, false);
        frame_record_t *f = find_frame(g, frame.value);
        if (g->core.life != PV2_CORE_RUNNING || !f || f->sealed || f->common.result != ESP_OK) {
            pv2_lock_exit(&g->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        for (size_t i = 0; i < g->config.buffer_count; ++i) {
            gram_slot_t *s = &g->slots[i];
            if (s->state != SLOT_FREE) {
                continue;
            }
            uint64_t token = pv2_new_token();
            if (!token) {
                pv2_lock_exit(&g->lock, false);
                return ESP_ERR_NO_MEM;
            }
            pv2_rect_t actual = *req_area;
            if (actual.height > g->config.tile_rows) {
                actual.height = g->config.tile_rows;
            }
            size_t bytes = g->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
            s->lease = (pv2_lease_t) {
                .id = { token }, .logical_area = actual,
                .surface = { s->pixels, g->tile_bytes,
                    { actual.width, actual.height, actual.width * bytes, g->config.format, g->config.draw_order }
                }
            };
            s->frame = (size_t)(f - g->core.frames);
            pv2_allocation_claim(&s->storage);
            s->state = SLOT_LEASED;
            ++f->leased;
            *out = s->lease;
            pv2_lock_exit(&g->lock, false);
            return ESP_OK;
        }
        pv2_lock_exit(&g->lock, false);
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static void revert_in_place_swap(void *pixels, size_t byte_count)
{
    uint8_t *p = pixels;
    for (size_t i = 0; i + 1 < byte_count; i += 2) {
        uint8_t tmp = p[i];
        p[i] = p[i + 1];
        p[i + 1] = tmp;
    }
}

static esp_err_t prepare_pixels(pv2_backend_t *g, gram_slot_t *s,
                                const pv2_rect_t *area, size_t src_stride,
                                pv2_surface_t *out, bool *out_swapped_in_place)
{
    size_t bytes = g->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t tight_stride = area->width * bytes;
    bool need_swap = g->config.draw_order != g->config.panel_order;
    bool need_pack = src_stride != tight_stride;
    bool quarter = g->config.rotation == PV2_ROTATE_90 || g->config.rotation == PV2_ROTATE_270;
    uint32_t dw = quarter ? area->height : area->width, dh = quarter ? area->width : area->height;
    *out_swapped_in_place = false;
    *out = (pv2_surface_t) {
        .pixels = s->pixels,
        .capacity_bytes = g->tile_bytes,
        .layout = { dw, dh, dw * bytes,
                    g->config.format, g->config.panel_order
                  },
    };
    if (!need_swap && !need_pack && g->config.rotation == PV2_ROTATE_0) {
        return ESP_OK;
    }
    if (need_pack || !g->config.in_place_convert || g->config.rotation != PV2_ROTATE_0) {
        if (!s->converted) {
            s->converted = pv2_alloc_pixels_in(g->tile_bytes, g->config.tile_in_psram);
            if (!s->converted) {
                return ESP_ERR_NO_MEM;
            }
        }
        out->pixels = s->converted;
    }
    pv2_copy_request_t request = {
        .source = {
            .pixels = s->pixels,
            .capacity_bytes = g->tile_bytes,
            .layout = {
                area->width, area->height, src_stride,
                g->config.format, g->config.draw_order
            },
        },
        .destination = *out,
        .source_area = { 0, 0, area->width, area->height },
        .destination_x = 0,
        .destination_y = 0,
        .rotation = g->config.rotation,
    };
    pv2_copy_ticket_t ticket;
    esp_err_t ret = pv2_pixel_submit(g->pixel, &request, &ticket);
    if (ret != ESP_OK) {
        return ret;
    }
    pv2_copy_status_t status;
    ret = pv2_pixel_wait(g->pixel, ticket, UINT32_MAX, &status);
    if (ret == ESP_OK) {
        ret = status.result;
    }
    (void)pv2_pixel_release(g->pixel, ticket);
    if (ret == ESP_OK && out->pixels == s->pixels && need_swap) {
        *out_swapped_in_place = true;
    }
    return ret;
}

static esp_err_t gram_submit_region_ex(pv2_backend_t *g, pv2_frame_t frame,
                                       pv2_lease_id_t lease,
                                       const pv2_region_submit_t *sub)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    frame_record_t *f = find_frame(g, frame.value);
    gram_slot_t *s = f ? find_lease(g, lease.value, (size_t)(f - g->core.frames)) : NULL;
    if (g->core.life != PV2_CORE_RUNNING || !f || f->sealed || f->common.result != ESP_OK || !s) {
        pv2_lock_exit(&g->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    pv2_rect_t submit_area = s->lease.logical_area;
    size_t bytes = g->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t src_stride = s->lease.surface.layout.stride_bytes;
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
    if (!valid_area(g, &submit_area) || src_stride < row_bytes || src_stride % bytes ||
            submit_area.height - 1 > (SIZE_MAX - row_bytes) / src_stride ||
            (submit_area.height - 1) * src_stride + row_bytes > g->tile_bytes) {
        pv2_lock_exit(&g->lock, false);
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_exit(&g->lock, false);

    pv2_rect_t physical_area;
    esp_err_t geometry = pv2_rotate_rect(g->config.width, g->config.height, g->config.rotation, &submit_area, &physical_area);
    if (geometry != ESP_OK) {
        return geometry;
    }
    if (g->config.async_copy) {
        uint64_t cookie = pv2_new_token();
        if (!cookie) {
            return ESP_ERR_NO_MEM;
        }
        pv2_surface_t target = {s->converted, g->tile_bytes,
            {physical_area.width, physical_area.height, physical_area.width * bytes, g->config.format, g->config.panel_order}
        };
        pv2_copy_request_t request = {
            .source = {
                s->pixels, g->tile_bytes,
                {submit_area.width, submit_area.height, src_stride, g->config.format, g->config.draw_order}
            },
            .destination = target, .source_area = {0, 0, submit_area.width, submit_area.height}, .rotation = g->config.rotation
        };
        pv2_copy_ticket_t ticket;
        esp_err_t error = pv2_pixel_submit(g->pixel, &request, &ticket);
        if (error != ESP_OK) {
            return error;
        }
        /* Task APIs serialized. Callback only wakes, never accesses this slot. */
        pv2_lock_enter(&g->lock, false);
        s->copy = ticket;
        s->cookie = cookie;
        s->tx = target;
        s->area = physical_area;
        pv2_allocation_written(&s->storage);
        pv2_allocation_hold(&s->storage, PV2_REF_COPY);
        pv2_allocation_hold(&s->storage, PV2_REF_TRANSFER);
        s->state = SLOT_COPYING;
        --f->leased;
        ++f->pending;
        pv2_lock_exit(&g->lock, false);
        return ESP_OK;
    }

    pv2_surface_t tx;
    bool swapped_in_place = false;
    esp_err_t ret = prepare_pixels(g, s, &submit_area, src_stride, &tx, &swapped_in_place);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t cookie = pv2_new_token();
    if (!cookie) {
        if (swapped_in_place) {
            revert_in_place_swap(s->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_NO_MEM;
    }
    pv2_lock_enter(&g->lock, false);
    /* An older transfer may have faulted while the CPU was converting. */
    if (f->common.result != ESP_OK) {
        pv2_lock_exit(&g->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(s->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (channel_full(g)) {
        pv2_lock_exit(&g->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(s->pixels, submit_area.height * row_bytes);
        }
        return ESP_ERR_NO_MEM;
    }
    pv2_allocation_written(&s->storage);
    pv2_allocation_hold(&s->storage, PV2_REF_TRANSFER);
    s->state = SLOT_SUBMITTING;
    s->cookie = cookie;
    pv2_transaction_prepare(&s->transaction);
    ++f->pending;
    pv2_lock_exit(&g->lock, false);

    pv2_tx_result_t result = g->config.panel.submit(g->config.panel.context, &tx, &physical_area, cookie);

    pv2_lock_enter(&g->lock, false);
    pv2_transaction_resolution_t resolved = pv2_transaction_resolve(&s->transaction, result);
    if (resolved.rejected) {
        --f->pending;
        pv2_allocation_drop(&s->storage, PV2_REF_TRANSFER);
        pv2_allocation_claim(&s->storage);
        s->state = SLOT_LEASED;
        s->cookie = 0;
        ret = result.result != ESP_OK ? result.result : ESP_FAIL;
        pv2_lock_exit(&g->lock, false);
        if (swapped_in_place) {
            revert_in_place_swap(s->pixels, submit_area.height * row_bytes);
        }
        return ret;
    }
    /* Early completion proves acceptance even if the adapter returned an
     * inconsistent rejection. Uncertain acceptance never returns a lease. */
    --f->leased;
    record_fault(g, f, cookie, resolved.result);
    if (resolved.complete) {
        complete_slot(g, s);
    } else {
        s->state = SLOT_INFLIGHT;
    }
    ret = ESP_OK;
    pv2_lock_exit(&g->lock, false);
    return ret;
}

static esp_err_t gram_submit_region(pv2_backend_t *g, pv2_frame_t frame, pv2_lease_id_t lease)
{
    return gram_submit_region_ex(g, frame, lease, NULL);
}

static esp_err_t gram_cancel_region(pv2_backend_t *g, pv2_frame_t frame, pv2_lease_id_t lease)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    frame_record_t *f = find_frame(g, frame.value);
    gram_slot_t *s = f ? find_lease(g, lease.value, (size_t)(f - g->core.frames)) : NULL;
    if (!f || f->sealed || !s) {
        pv2_lock_exit(&g->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    --f->leased;
    pv2_allocation_written(&s->storage);
    s->state = SLOT_FREE;
    pv2_lock_exit(&g->lock, false);
    return ESP_OK;
}

static esp_err_t gram_get_draw_buffers(pv2_backend_t *g, void **out_buffers,
                                       size_t max_buffers, size_t *out_count,
                                       size_t *out_buffer_bytes)
{
    if (!g || !out_count || (max_buffers && !out_buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = g->config.buffer_count;
    if (out_buffer_bytes) {
        *out_buffer_bytes = g->tile_bytes;
    }
    if (max_buffers < g->config.buffer_count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < g->config.buffer_count; ++i) {
        out_buffers[i] = g->slots[i].pixels;
    }
    return ESP_OK;
}

static esp_err_t gram_end_ex(pv2_backend_t *g, pv2_frame_t frame,
                             const pv2_frame_coverage_t *coverage,
                             pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    if (coverage && pv2_validate_damage(g->config.width, g->config.height,
                                        coverage->areas, coverage->area_count) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    frame_record_t *f = find_frame(g, frame.value);
    esp_err_t ret = pv2_frame_core_seal(&g->core, f, out);
    pv2_lock_exit(&g->lock, false);
    return ret;
}

static esp_err_t gram_end(pv2_backend_t *g, pv2_frame_t frame, pv2_receipt_t *out)
{
    return gram_end_ex(g, frame, NULL, out);
}

static void cancel_build(pv2_backend_t *g, size_t index)
{
    frame_record_t *f = &g->core.frames[index];
    f->sealed = true;
    f->canceled = true;
    for (size_t i = 0; i < g->config.buffer_count; ++i) {
        gram_slot_t *s = &g->slots[i];
        if (s->state == SLOT_LEASED && s->frame == index) {
            pv2_allocation_written(&s->storage);
            s->state = SLOT_FREE;
            --f->leased;
        }
    }
}

static esp_err_t gram_cancel_frame(pv2_backend_t *g, pv2_frame_t frame, uint32_t timeout)
{
    if (!g || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        pv2_lock_enter(&g->lock, false);
        frame_record_t *f = find_frame(g, frame.value);
        if (!f) {
            if (pv2_cancel_matches(&g->core.canceled, frame.value)) {
                pv2_lock_exit(&g->lock, false);
                return ESP_OK;
            }
            pv2_lock_exit(&g->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        if (f->common.external || (f->sealed && !f->canceled)) {
            pv2_lock_exit(&g->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        cancel_build(g, (size_t)(f - g->core.frames));
        if (!f->pending) {
            pv2_frame_core_reap(&g->core, f);
            pv2_lock_exit(&g->lock, false);
            return ESP_OK;
        }
        pv2_lock_exit(&g->lock, false);
        (void)pv2_gram_service(g);
        pv2_lock_enter(&g->lock, false);
        bool canceled = pv2_cancel_matches(&g->core.canceled, frame.value);
        pv2_lock_exit(&g->lock, false);
        if (canceled) {
            return ESP_OK;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static esp_err_t gram_query(pv2_backend_t *g, pv2_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!g || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_gram_service(g);
    pv2_lock_enter(&g->lock, false);
    frame_record_t *f = find_frame(g, receipt.value);
    if (!f || !f->common.external) {
        pv2_lock_exit(&g->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    *out = pv2_record_status(&f->common, f->sealed, f->pending == 0, f->sealed && f->pending == 0, false);
    pv2_lock_exit(&g->lock, false);
    return ESP_OK;
}

static esp_err_t gram_get_fault(pv2_backend_t *g, pv2_backend_fault_t *out)
{
    if (!g || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    *out = g->core.fault;
    pv2_lock_exit(&g->lock, false);
    return ESP_OK;
}

static esp_err_t gram_wait(pv2_backend_t *g, pv2_receipt_t receipt, pv2_completion_point_t point,
                           uint32_t timeout, pv2_frame_status_t *out)
{
    if (!g || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (point != PV2_COMPLETION_SOURCE_RELEASED) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        esp_err_t ret = gram_query(g, receipt, out);
        if (ret != ESP_OK || out->terminal) {
            return ret;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    }
}

static esp_err_t gram_release_receipt(pv2_backend_t *g, pv2_receipt_t receipt)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    frame_record_t *f = find_frame(g, receipt.value);
    esp_err_t ret = pv2_frame_core_release(f);
    pv2_lock_exit(&g->lock, false);
    return ret;
}

static esp_err_t gram_stop(pv2_backend_t *g, uint32_t timeout)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&g->lock, false);
    if (g->core.life == PV2_CORE_QUIESCENT) {
        pv2_lock_exit(&g->lock, false);
        return ESP_OK;
    }
    g->core.life = PV2_CORE_CLOSING;
    if (g->core.active != SIZE_MAX) {
        cancel_build(g, g->core.active);
    }
    pv2_lock_exit(&g->lock, false);
    uint64_t start = pv2_now_ms();
    esp_err_t ret = pv2_pixel_stop(g->pixel, timeout);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_gram_service(g); /* Closing: only retire copies, never submit. */
    uint32_t left = pv2_remaining_ms(start, timeout);
    ret = g->config.panel.quiesce(g->config.panel.context, left);
    if (ret != ESP_OK) {
        return ret;
    }
    /* Adapter now guarantees no access/callback. Pending transfers may have
     * been safely aborted; do not falsely report their frames successful. */
    pv2_lock_enter(&g->lock, false);
    for (size_t i = 0; i < g->config.buffer_count; ++i) {
        gram_slot_t *s = &g->slots[i];
        if (s->state == SLOT_INFLIGHT) {
            frame_record_t *f = &g->core.frames[s->frame];
            record_fault(g, f, s->cookie, ESP_ERR_INVALID_STATE);
            complete_slot(g, s);
        }
    }
    ret = pv2_frame_core_quiescent(&g->core);
    pv2_lock_exit(&g->lock, false);
    return ret;
}

static esp_err_t gram_destroy(pv2_backend_t *g)
{
    if (!g) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g->core.life != PV2_CORE_QUIESCENT) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < g->config.max_frames; ++i) {
        if (g->core.frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    free_backend(g);
    return ESP_OK;
}

static esp_err_t gram_set_rotation(pv2_backend_t *g, pv2_rotation_t rotation)
{
    if (!g || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g->core.life != PV2_CORE_RUNNING || g->core.active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < g->config.max_frames; ++i)
        if (g->core.frames[i].common.token) {
            return ESP_ERR_INVALID_STATE;
        }
    if (g->config.in_place_convert && rotation != PV2_ROTATE_0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool change_axes = ((unsigned)g->config.rotation ^ (unsigned)rotation) & 1;
    uint32_t width = change_axes ? g->config.height : g->config.width;
    uint32_t height = change_axes ? g->config.width : g->config.height;
    size_t bpp = g->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t rows = g->tile_bytes / ((size_t)width * bpp);
    if (!rows) {
        return ESP_ERR_INVALID_SIZE;
    }
    g->config.width = width;
    g->config.height = height;
    g->config.tile_rows = rows > height ? height : rows;
    g->config.rotation = rotation;
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

size_t pv2_gram_memory(const pv2_backend_config_t *c)
{
    return sizeof(pv2_backend_t) + c->buffer_count * sizeof(gram_slot_t) + c->max_frames * sizeof(pv2_core_frame_t) +
           pv2_pixel_memory(c->buffer_count, c->async_copy);
}
