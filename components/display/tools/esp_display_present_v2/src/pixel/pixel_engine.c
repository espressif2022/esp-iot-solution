/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "present_pixel_engine.h"
#include "present_os.h"
#include "present_limits.h"
#include <string.h>

typedef struct {
    uint64_t token;
    pv2_copy_status_t status;
    pv2_copy_request_t request;
    pv2_event_t *changed;
    bool waiting;
    bool canceled;
} copy_record_t;

struct pv2_pixel_engine {
    pv2_lock_t lock;
    size_t count;
    pv2_pixel_event_fn on_event;
    void *user;
    bool closing;
    bool asynchronous;
    pv2_pixel_execute_fn execute;
    void *execute_user;
    pv2_event_t *work;
    pv2_event_t *idle;
    pv2_worker_t *worker;
    bool stopped;
    bool executing; /* Serialize CPU writes, including overlapping destinations. */
    bool notifying;
    pv2_task_t callback_task;
    copy_record_t records[];
};

static bool layout_span(pv2_layout_t layout, size_t capacity, size_t *span)
{
    size_t bytes = layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if ((layout.format != PV2_FORMAT_RGB565 && layout.format != PV2_FORMAT_RGB888) ||
            (layout.byte_order != PV2_ORDER_NATIVE && layout.byte_order != PV2_ORDER_SWAPPED) ||
            (layout.format == PV2_FORMAT_RGB888 && layout.byte_order != PV2_ORDER_NATIVE) ||
            !layout.width || !layout.height || layout.width > SIZE_MAX / bytes) {
        return false;
    }
    size_t row = layout.width * bytes;
    if (layout.stride_bytes < row || layout.stride_bytes % bytes ||
            layout.height - 1 > (SIZE_MAX - row) / layout.stride_bytes) {
        return false;
    }
    *span = (layout.height - 1) * layout.stride_bytes + row;
    return *span <= capacity;
}

static esp_err_t validate(const pv2_copy_request_t *r)
{
    size_t src_span, dst_span;
    if (!r || !r->source.pixels || !r->destination.pixels ||
            !layout_span(r->source.layout, r->source.capacity_bytes, &src_span) ||
            !layout_span(r->destination.layout, r->destination.capacity_bytes, &dst_span) ||
            r->source.layout.format != r->destination.layout.format ||
            r->rotation < PV2_ROTATE_0 || r->rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t a = r->source_area;
    if (!a.width || !a.height || a.x > r->source.layout.width ||
            a.y > r->source.layout.height ||
            a.width > r->source.layout.width - a.x || a.height > r->source.layout.height - a.y) {
        return ESP_ERR_INVALID_ARG;
    }
    bool quarter = r->rotation == PV2_ROTATE_90 || r->rotation == PV2_ROTATE_270;
    uint32_t w = quarter ? a.height : a.width, h = quarter ? a.width : a.height;
    if (r->destination_x > r->destination.layout.width ||
            r->destination_y > r->destination.layout.height ||
            w > r->destination.layout.width - r->destination_x ||
            h > r->destination.layout.height - r->destination_y) {
        return ESP_ERR_INVALID_ARG;
    }
    uintptr_t src = (uintptr_t)r->source.pixels, dst = (uintptr_t)r->destination.pixels;
    if (src > UINTPTR_MAX - src_span || dst > UINTPTR_MAX - dst_span) {
        return ESP_ERR_INVALID_ARG;
    }
    if (src == dst) {
        if (r->rotation != PV2_ROTATE_0 || a.x != r->destination_x ||
                a.y != r->destination_y ||
                r->source.layout.width != r->destination.layout.width ||
                r->source.layout.height != r->destination.layout.height ||
                r->source.layout.stride_bytes != r->destination.layout.stride_bytes) {
            return ESP_ERR_INVALID_ARG;
        }
    } else if (src < dst + dst_span && dst < src + src_span) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

static void PV2_ISR_ATTR swap_row_rgb565(uint16_t *dst, const uint16_t *src, uint32_t width)
{
    uint32_t x = 0;
    if ((((uintptr_t)dst | (uintptr_t)src) & 3u) == 0) {
        uint32_t *d32 = (uint32_t *)dst;
        const uint32_t *s32 = (const uint32_t *)src;
        for (; x + 2 <= width; x += 2) {
            uint32_t v = *s32++;
            *d32++ = ((v & 0x00ff00ffu) << 8) | ((v & 0xff00ff00u) >> 8);
        }
    }
    for (; x < width; ++x) {
        dst[x] = swap16(src[x]);
    }
}

static void PV2_ISR_ATTR copy_cpu(const pv2_copy_request_t *r)
{
    size_t bytes = r->source.layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    bool swap = r->source.layout.byte_order != r->destination.layout.byte_order;
    bool in_place = r->source.pixels == r->destination.pixels;
    if (in_place && !swap) {
        return;
    }
    pv2_rect_t a = r->source_area;
    size_t src_stride = r->source.layout.stride_bytes;
    size_t dst_stride = r->destination.layout.stride_bytes;

    if (r->rotation == PV2_ROTATE_0) {
        const uint8_t *src_row = (const uint8_t *)r->source.pixels +
                                 (size_t)a.y * src_stride + (size_t)a.x * bytes;
        uint8_t *dst_row = (uint8_t *)r->destination.pixels +
                           (size_t)r->destination_y * dst_stride + (size_t)r->destination_x * bytes;
        if (!swap) {
            size_t row_bytes = (size_t)a.width * bytes;
            if (row_bytes == src_stride && src_stride == dst_stride) {
                memcpy(dst_row, src_row, row_bytes * a.height);
            } else {
                for (uint32_t y = 0; y < a.height; ++y) {
                    memcpy(dst_row, src_row, row_bytes);
                    src_row += src_stride;
                    dst_row += dst_stride;
                }
            }
            return;
        }
        if (bytes == 2 && (((uintptr_t)src_row | (uintptr_t)dst_row | src_stride | dst_stride) & 1u) == 0) {
            for (uint32_t y = 0; y < a.height; ++y) {
                swap_row_rgb565((uint16_t *)dst_row, (const uint16_t *)src_row, a.width);
                src_row += src_stride;
                dst_row += dst_stride;
            }
            return;
        }
    }

    if (bytes == 2 && (((uintptr_t)r->source.pixels | (uintptr_t)r->destination.pixels | src_stride | dst_stride) & 1u) == 0) {
        if (r->rotation == PV2_ROTATE_180) {
            for (uint32_t y = 0; y < a.height; ++y) {
                const uint16_t *src_px = (const uint16_t *)((const uint8_t *)r->source.pixels +
                                                            (size_t)(a.y + y) * src_stride) + a.x + a.width - 1;
                uint16_t *dst_px = (uint16_t *)((uint8_t *)r->destination.pixels +
                                                (size_t)(r->destination_y + a.height - 1 - y) * dst_stride) + r->destination_x;
                uint32_t n = a.width;
                if (((uintptr_t)dst_px & 3u) != 0 && n > 0) {
                    uint16_t v = *src_px--;
                    *dst_px++ = swap ? swap16(v) : v;
                    n--;
                }
                uint32_t *dst32 = (uint32_t *)dst_px;
                for (; n >= 2; n -= 2) {
                    uint32_t lo = *src_px--;
                    uint32_t hi = *src_px--;
                    uint32_t pair = lo | (hi << 16);
                    *dst32++ = swap ? (((pair & 0x00ff00ffu) << 8) | ((pair & 0xff00ff00u) >> 8)) : pair;
                }
                if (n != 0) {
                    uint16_t v = *src_px;
                    *(uint16_t *)dst32 = swap ? swap16(v) : v;
                }
            }
            return;
        }
        if (r->rotation == PV2_ROTATE_90 || r->rotation == PV2_ROTATE_270) {
            const uint32_t bw = 32, bh = 64;
            size_t src_stride_px = src_stride / 2;
            for (uint32_t bx = 0; bx < a.width; bx += bw) {
                uint32_t x_end = bx + bw < a.width ? bx + bw : a.width;
                for (uint32_t by = 0; by < a.height; by += bh) {
                    uint32_t y_end = by + bh < a.height ? by + bh : a.height;
                    uint32_t run = y_end - by;
                    for (uint32_t x = bx; x < x_end; ++x) {
                        const uint16_t *src_px;
                        uint16_t *dst_px;
                        ptrdiff_t src_step;
                        if (r->rotation == PV2_ROTATE_90) {
                            uint32_t dy = x;
                            uint32_t dx = a.height - y_end;
                            dst_px = (uint16_t *)((uint8_t *)r->destination.pixels +
                                                  (size_t)(r->destination_y + dy) * dst_stride) + r->destination_x + dx;
                            src_px = (const uint16_t *)((const uint8_t *)r->source.pixels +
                                                        (size_t)(a.y + y_end - 1) * src_stride) + a.x + x;
                            src_step = -(ptrdiff_t)src_stride_px;
                        } else {
                            uint32_t dy = a.width - 1 - x;
                            uint32_t dx = by;
                            dst_px = (uint16_t *)((uint8_t *)r->destination.pixels +
                                                  (size_t)(r->destination_y + dy) * dst_stride) + r->destination_x + dx;
                            src_px = (const uint16_t *)((const uint8_t *)r->source.pixels +
                                                        (size_t)(a.y + by) * src_stride) + a.x + x;
                            src_step = (ptrdiff_t)src_stride_px;
                        }
                        uint32_t n = run;
                        if (((uintptr_t)dst_px & 3u) != 0 && n > 0) {
                            uint16_t v = *src_px;
                            src_px += src_step;
                            *dst_px++ = swap ? swap16(v) : v;
                            n--;
                        }
                        uint32_t *dst32 = (uint32_t *)dst_px;
                        if (!swap) {
                            for (; n >= 2; n -= 2) {
                                uint32_t lo = *src_px;
                                src_px += src_step;
                                uint32_t hi = *src_px;
                                src_px += src_step;
                                *dst32++ = lo | (hi << 16);
                            }
                        } else {
                            for (; n >= 2; n -= 2) {
                                uint32_t lo = *src_px;
                                src_px += src_step;
                                uint32_t hi = *src_px;
                                src_px += src_step;
                                uint32_t pair = lo | (hi << 16);
                                *dst32++ = ((pair & 0x00ff00ffu) << 8) | ((pair & 0xff00ff00u) >> 8);
                            }
                        }
                        if (n != 0) {
                            uint16_t v = *src_px;
                            *(uint16_t *)dst32 = swap ? swap16(v) : v;
                        }
                    }
                }
            }
            return;
        }
    }

    for (uint32_t y = 0; y < a.height; ++y) {
        for (uint32_t x = 0; x < a.width; ++x) {
            uint32_t dx = x, dy = y;
            switch (r->rotation) {
            case PV2_ROTATE_90:
                dx = a.height - 1 - y;
                dy = x;
                break;
            case PV2_ROTATE_180:
                dx = a.width - 1 - x;
                dy = a.height - 1 - y;
                break;
            case PV2_ROTATE_270:
                dx = y;
                dy = a.width - 1 - x;
                break;
            default:
                break;
            }
            const uint8_t *src = (const uint8_t *)r->source.pixels +
                                 (a.y + y) * src_stride + (a.x + x) * bytes;
            uint8_t *dst = (uint8_t *)r->destination.pixels +
                           (r->destination_y + dy) * dst_stride +
                           (r->destination_x + dx) * bytes;
            if (swap) {
                uint8_t b0 = src[0], b1 = src[1];
                dst[0] = b1;
                dst[1] = b0;
            } else {
                memcpy(dst, src, bytes);
            }
        }
    }
}

/* The record may be released by the callback; no access to it after cb. */
static void finish_copy(pv2_pixel_engine_t *e, copy_record_t *record, esp_err_t result)
{
    pv2_lock_enter(&e->lock, false);
    record->status.state = result == ESP_OK ? PV2_COPY_SUCCEEDED : PV2_COPY_FAILED;
    record->status.result = result;
    pv2_copy_status_t status = record->status;
    pv2_copy_ticket_t ticket = {record->token};
    pv2_pixel_event_fn cb = e->on_event;
    e->notifying = cb != NULL;
    if (cb) {
        e->callback_task = pv2_current_task();
    }
    /* Publish while record ownership is stable, before callback can release it. */
    pv2_event_t *changed = record->changed;
    pv2_lock_exit(&e->lock, false);
    (void)pv2_event_signal(changed, false);
    if (cb) {
        (void)cb(e->user, ticket, &status, false);
    }
    pv2_lock_enter(&e->lock, false);
    e->notifying = false;
    e->executing = false;
    pv2_lock_exit(&e->lock, false);
    (void)pv2_event_signal(e->idle, false);
}

static void copy_worker(void *context)
{
    pv2_pixel_engine_t *e = context;
    for (;;) {
        pv2_lock_enter(&e->lock, false);
        copy_record_t *next = NULL;
        for (size_t i = 0; i < e->count; i++) {
            copy_record_t *record = &e->records[i];
            if (record->token && record->status.state == PV2_COPY_QUEUED &&
                    (!next || record->token < next->token)) {
                next = record;
            }
        }
        if (!next) {
            bool exit = e->closing;
            pv2_lock_exit(&e->lock, false);
            if (exit) {
                return;
            }
            (void)pv2_event_wait(e->work, UINT32_MAX);
            continue;
        }
        bool canceled = next->canceled;
        next->status.state = PV2_COPY_RUNNING;
        e->executing = true;
        pv2_copy_request_t request = next->request;
        pv2_lock_exit(&e->lock, false);
        esp_err_t result = ESP_ERR_INVALID_STATE;
        if (!canceled) {
            if (e->execute) {
                result = e->execute(e->execute_user, &request);
            } else {
                copy_cpu(&request);
                result = ESP_OK;
            }
        }
        finish_copy(e, next, result);
    }
}

static void free_engine(pv2_pixel_engine_t *e)
{
    for (size_t i = 0; i < e->count; i++) {
        pv2_event_destroy(e->records[i].changed);
    }
    pv2_worker_destroy(e->worker);
    pv2_event_destroy(e->work);
    pv2_event_destroy(e->idle);
    pv2_lock_destroy(&e->lock);
    pv2_free(e);
}

esp_err_t pv2_pixel_create(const pv2_pixel_config_t *config, pv2_pixel_engine_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->max_jobs || config->max_jobs > PV2_MAX_COPY_JOBS || (config->execute && !config->asynchronous)) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_pixel_engine_t *e = pv2_alloc(sizeof(*e) + config->max_jobs * sizeof(copy_record_t));
    if (!e) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&e->lock)) {
        pv2_free(e);
        return ESP_ERR_NO_MEM;
    }
    e->count = config->max_jobs;
    e->on_event = config->on_event;
    e->user = config->user;
    e->asynchronous = config->asynchronous;
    e->execute = config->execute;
    e->execute_user = config->execute_user;
    e->work = pv2_event_create();
    e->idle = pv2_event_create();
    if (!e->work || !e->idle) {
        free_engine(e);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < e->count; i++) {
        e->records[i].changed = pv2_event_create();
        if (!e->records[i].changed) {
            free_engine(e);
            return ESP_ERR_NO_MEM;
        }
    }
    if (e->asynchronous) {
        e->worker = pv2_worker_create(copy_worker, e, 4096, 5);
        if (!e->worker) {
            free_engine(e);
            return ESP_ERR_NO_MEM;
        }
    }
    *out = e;
    return ESP_OK;
}

esp_err_t pv2_pixel_submit(pv2_pixel_engine_t *e, const pv2_copy_request_t *r, pv2_copy_ticket_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!e) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = validate(r);
    if (result != ESP_OK) {
        return result;
    }
    pv2_lock_enter(&e->lock, false);
    if (e->closing) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if ((!e->asynchronous && e->executing) ||
            (e->notifying && pv2_same_task(e->callback_task, pv2_current_task()))) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_NO_MEM;
    }
    copy_record_t *record = NULL;
    for (size_t i = 0; i < e->count; i++) {
        if (!e->records[i].token) {
            record = &e->records[i];
            break;
        }
    }
    uint64_t token = record ? pv2_new_token() : 0;
    if (!token) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_NO_MEM;
    }
    record->token = token;
    record->status = (pv2_copy_status_t) {
        .state = e->asynchronous ? PV2_COPY_QUEUED : PV2_COPY_RUNNING
    };
    record->request = *r;
    record->canceled = false;
    out->value = token;
    if (!e->asynchronous) {
        e->executing = true;
    }
    pv2_lock_exit(&e->lock, false);
    if (e->asynchronous) {
        (void)pv2_event_signal(e->work, false);
    } else {
        copy_cpu(r);
        finish_copy(e, record, ESP_OK);
    }
    return ESP_OK;
}

static copy_record_t *find_record(pv2_pixel_engine_t *e, pv2_copy_ticket_t ticket)
{
    for (size_t i = 0; i < e->count; i++) {
        if (e->records[i].token == ticket.value) {
            return &e->records[i];
        }
    }
    return NULL;
}

esp_err_t pv2_pixel_query(pv2_pixel_engine_t *e, pv2_copy_ticket_t ticket, pv2_copy_status_t *out)
{
    if (!e || !out || !ticket.value) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&e->lock, false);
    copy_record_t *record = find_record(e, ticket);
    if (record) {
        *out = record->status;
    }
    pv2_lock_exit(&e->lock, false);
    return record ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool terminal(pv2_copy_status_t status)
{
    return status.state == PV2_COPY_SUCCEEDED || status.state == PV2_COPY_FAILED;
}

esp_err_t pv2_pixel_wait(pv2_pixel_engine_t *e, pv2_copy_ticket_t ticket, uint32_t timeout, pv2_copy_status_t *out)
{
    if (!e || !out || !ticket.value) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    pv2_lock_enter(&e->lock, false);
    copy_record_t *record = find_record(e, ticket);
    if (!record || record->waiting) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (e->notifying && pv2_same_task(e->callback_task, pv2_current_task()) && !terminal(record->status)) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    record->waiting = true;
    for (;;) {
        *out = record->status;
        if (terminal(*out) || pv2_expired(start, timeout)) {
            record->waiting = false;
            pv2_lock_exit(&e->lock, false);
            return terminal(*out) ? ESP_OK : ESP_ERR_TIMEOUT;
        }
        pv2_lock_exit(&e->lock, false);
        uint64_t elapsed = pv2_now_ms() - start;
        uint32_t left = timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : timeout - elapsed;
        (void)pv2_event_wait(record->changed, left);
        pv2_lock_enter(&e->lock, false);
    }
}

esp_err_t pv2_pixel_cancel(pv2_pixel_engine_t *e, pv2_copy_ticket_t ticket, uint32_t timeout, pv2_copy_status_t *out)
{
    if (!e || !ticket.value || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&e->lock, false);
    copy_record_t *record = find_record(e, ticket);
    if (!record) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (record->status.state == PV2_COPY_QUEUED) {
        record->canceled = true;
    }
    pv2_lock_exit(&e->lock, false);
    (void)pv2_event_signal(e->work, false);
    return pv2_pixel_wait(e, ticket, timeout, out);
}

esp_err_t pv2_pixel_release(pv2_pixel_engine_t *e, pv2_copy_ticket_t ticket)
{
    if (!e || !ticket.value) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&e->lock, false);
    copy_record_t *record = find_record(e, ticket);
    if (!record || record->waiting || !terminal(record->status)) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    record->token = 0;
    pv2_lock_exit(&e->lock, false);
    return ESP_OK;
}

esp_err_t pv2_pixel_stop(pv2_pixel_engine_t *e, uint32_t timeout)
{
    if (!e) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    pv2_lock_enter(&e->lock, false);
    if (e->notifying && pv2_same_task(e->callback_task, pv2_current_task())) {
        pv2_lock_exit(&e->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    e->closing = true;
    pv2_lock_exit(&e->lock, false);
    (void)pv2_event_signal(e->work, false);
    if (e->asynchronous) {
        if (!pv2_worker_join(e->worker, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
    } else {
        for (;;) {
            pv2_lock_enter(&e->lock, false);
            bool busy = e->executing;
            pv2_lock_exit(&e->lock, false);
            if (!busy) {
                break;
            }
            uint64_t elapsed = pv2_now_ms() - start;
            if (timeout != UINT32_MAX && elapsed >= timeout) {
                return ESP_ERR_TIMEOUT;
            }
            (void)pv2_event_wait(e->idle, timeout == UINT32_MAX ? UINT32_MAX : timeout - elapsed);
        }
    }
    e->stopped = true;
    return ESP_OK;
}

esp_err_t pv2_pixel_destroy(pv2_pixel_engine_t *e)
{
    if (!e) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!e->stopped) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < e->count; i++) {
        if (e->records[i].token || e->records[i].waiting) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    free_engine(e);
    return ESP_OK;
}

esp_err_t pv2_rotate_rect(uint32_t w, uint32_t h, pv2_rotation_t rotation,
                          const pv2_rect_t *a, pv2_rect_t *out)
{
    if (!a || !out || !w || !h || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270 ||
            !a->width || !a->height || a->x >= w || a->y >= h || a->width > w - a->x || a->height > h - a->y) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_rect_t r = *a;
    switch (rotation) {
    case PV2_ROTATE_90:
        r = (pv2_rect_t) {
            h - a->y - a->height, a->x, a->height, a->width
        };
        break;
    case PV2_ROTATE_180:
        r.x = w - a->x - a->width;
        r.y = h - a->y - a->height;
        break;
    case PV2_ROTATE_270:
        r = (pv2_rect_t) {
            a->y, w - a->x - a->width, a->height, a->width
        };
        break;
    default:
        break;
    }
    *out = r;
    return ESP_OK;
}

esp_err_t pv2_unrotate_point(uint32_t w, uint32_t h, pv2_rotation_t rotation,
                             uint32_t x, uint32_t y, uint32_t *lx, uint32_t *ly)
{
    if (!lx || !ly || !w || !h || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    bool q = rotation == PV2_ROTATE_90 || rotation == PV2_ROTATE_270;
    if (x >= (q ? h : w) || y >= (q ? w : h)) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (rotation) {
    case PV2_ROTATE_90:
        *lx = y;
        *ly = h - 1 - x;
        break;
    case PV2_ROTATE_180:
        *lx = w - 1 - x;
        *ly = h - 1 - y;
        break;
    case PV2_ROTATE_270:
        *lx = w - 1 - y;
        *ly = x;
        break;
    default:
        *lx = x;
        *ly = y;
        break;
    }
    return ESP_OK;
}

esp_err_t pv2_pixel_validate_request(const pv2_copy_request_t *request)
{
    return validate(request);
}

esp_err_t pv2_pixel_execute_cpu(const pv2_copy_request_t *request)
{
    esp_err_t error = validate(request);
    if (error == ESP_OK) {
        copy_cpu(request);
    }
    return error;
}

size_t pv2_pixel_memory(size_t jobs, bool async)
{
    return sizeof(pv2_pixel_engine_t) + jobs * sizeof(copy_record_t) + (jobs + 2) * pv2_event_memory() +
           (async ? pv2_worker_memory() : 0);
}
