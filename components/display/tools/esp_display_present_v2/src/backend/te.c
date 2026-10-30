/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_records.h"
#include "te_composer.h"

enum { TE_MAX_BUFFERS = PV2_MAX_TILE_BUFFERS, TE_MAX_FRAMES = PV2_MAX_FRAMES, TE_MAX_DAMAGE = PV2_MAX_DAMAGE_AREAS };
typedef enum { TE_RUNNING, TE_CLOSING, TE_QUIESCENT } te_lifecycle_t;
typedef enum { TE_IDLE, TE_BUILDING, TE_COPY_DRAIN, TE_CANCELING, TE_READY, TE_SUBMITTING, TE_INFLIGHT } te_phase_t;
typedef pv2_record_t te_record_t;
typedef struct {
    void *pixels;
    pv2_copy_ticket_t copy;
    pv2_lease_t lease; /* id == 0 when not borrowed by the producer. */
} te_tile_t;

struct pv2_backend {
    pv2_lock_t lock;
    pv2_backend_config_t config;
    pv2_te_composer_t *composer; /* Task-only; never accessed by callbacks. */
    pv2_te_frame_t compose_frame;
    te_tile_t *tiles;
    te_record_t *records;
    size_t tile_bytes;
    size_t active;
    te_lifecycle_t lifecycle;
    te_phase_t phase;
    uint64_t cookie;
    uint64_t latest_edge;
    bool has_edge;
    bool done; /* Completion mailbox, including early completion. */
    pv2_cancel_history_t canceled;
    pv2_backend_fault_t fault;
};

static bool valid_area(const pv2_backend_t *backend, const pv2_rect_t *area)
{
    return pv2_valid_rect(backend->config.width, backend->config.height, area);
}

/* Lock held. Only accepted operations/channel faults use this sticky record. */
static void PV2_ISR_ATTR record_fault(pv2_backend_t *backend, esp_err_t error)
{
    if (error == ESP_OK) {
        return;
    }
    uint64_t token = 0;
    if (backend->active != SIZE_MAX) {
        te_record_t *record = &backend->records[backend->active];
        token = record->token;
        pv2_record_fail(record, error);
    }
    pv2_fault_first(&backend->fault, token, backend->cookie, error);
}

static bool PV2_ISR_ATTR te_dispatch(void *user, const pv2_backend_event_t *event, bool isr)
{
    pv2_backend_t *backend = user;
    if (!backend || !event) {
        return false;
    }
    pv2_lock_enter(&backend->lock, isr);
    if (backend->lifecycle != TE_QUIESCENT) {
        if (event->kind == PV2_EVENT_TE_EDGE && backend->lifecycle == TE_RUNNING) {
            if (!backend->has_edge || event->timestamp_us > backend->latest_edge) {
                backend->latest_edge = event->timestamp_us;
                backend->has_edge = true;
            }
        } else if (event->kind == PV2_EVENT_FAULT && !event->transaction_cookie) {
            record_fault(backend, event->result == ESP_OK ? ESP_FAIL : event->result);
        } else if (backend->cookie && event->transaction_cookie == backend->cookie &&
                   (backend->phase == TE_SUBMITTING || backend->phase == TE_INFLIGHT)) {
            if (event->kind == PV2_EVENT_TRANSFER_DONE) {
                record_fault(backend, event->result);
                backend->done = true;
            } else if (event->kind == PV2_EVENT_FAULT) {
                record_fault(backend, event->result == ESP_OK ? ESP_FAIL : event->result);
            }
        }
    }
    pv2_lock_exit(&backend->lock, isr);
    return false; /* Cooperative pump; no task waiter is registered yet. */
}

static bool PV2_ISR_ATTR te_on_event_isr(pv2_backend_t *backend, const pv2_backend_event_t *event)
{
    return te_dispatch(backend, event, true);
}

static void te_on_event(pv2_backend_t *backend, const pv2_backend_event_t *event)
{
    (void)te_dispatch(backend, event, false);
}

/* Task-only, lock held, after composer no longer borrows the candidate. */
static void retire_frame(pv2_backend_t *backend)
{
    te_record_t *record = &backend->records[backend->active];
    record->terminal = true;
    if (!record->external) {
        record->token = 0;
    }
    backend->active = SIZE_MAX;
    backend->phase = TE_IDLE;
    backend->cookie = 0;
    backend->done = false;
}

/* Pixel callback cannot mutate composer/tiles; owner service does that. */
static bool copy_done(void *user, pv2_copy_ticket_t ticket,
                      const pv2_copy_status_t *status, bool isr)
{
    (void)ticket;
    (void)status;
    pv2_backend_t *b = user;
    return b->config.on_progress ? b->config.on_progress(b->config.progress_user, isr) : false;
}

static size_t progress_copies(pv2_backend_t *b)
{
    size_t pending = 0;
    for (size_t i = 0; i < b->config.buffer_count; ++i) {
        te_tile_t *tile = &b->tiles[i];
        if (!tile->copy.value) {
            continue;
        }
        esp_err_t result = ESP_OK;
        esp_err_t error = pv2_te_composer_finish_write(b->composer, tile->copy, &result);
        if (error != ESP_OK) {
            pending++;
            continue;
        }
        tile->copy.value = 0;
        if (result != ESP_OK) {
            pv2_lock_enter(&b->lock, false);
            record_fault(b, result);
            pv2_lock_exit(&b->lock, false);
        }
    }
    return pending;
}

esp_err_t pv2_te_service(pv2_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t pending = progress_copies(backend);
    if (!pending && (backend->phase == TE_COPY_DRAIN || backend->phase == TE_CANCELING)) {
        bool cancel = backend->phase == TE_CANCELING;
        pv2_lock_enter(&backend->lock, false);
        esp_err_t error = backend->fault.result;
        pv2_lock_exit(&backend->lock, false);
        if (!cancel && error == ESP_OK && backend->lifecycle == TE_RUNNING) {
            error = pv2_te_composer_seal(backend->composer, backend->compose_frame, pv2_now_us());
        } else if (!cancel && error == ESP_OK) {
            return ESP_OK; /* stop owns drain, never seals or sends. */
        }
        if (error == ESP_ERR_TIMEOUT) {
            return ESP_OK;
        }
        if (cancel || error != ESP_OK) {
            if (pv2_te_composer_cancel(backend->composer, backend->compose_frame) == ESP_ERR_TIMEOUT) {
                return ESP_OK;
            }
            pv2_lock_enter(&backend->lock, false);
            if (cancel) {
                pv2_cancel_publish(&backend->canceled, backend->compose_frame.value);
            } else {
                record_fault(backend, error);
            }
            retire_frame(backend);
            pv2_lock_exit(&backend->lock, false);
        } else {
            pv2_lock_enter(&backend->lock, false);
            backend->phase = TE_READY;
            backend->has_edge = false;
            pv2_lock_exit(&backend->lock, false);
        }
    }
    pv2_lock_enter(&backend->lock, false);
    if (backend->phase == TE_INFLIGHT && backend->done) {
        esp_err_t result = backend->records[backend->active].result;
        /* This changes only composer metadata, never copies/frees memory. */
        (void)pv2_te_composer_complete(backend->composer, backend->compose_frame, result);
        retire_frame(backend);
    }
    if (backend->lifecycle != TE_RUNNING || backend->phase != TE_READY) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    if (backend->fault.result != ESP_OK) {
        (void)pv2_te_composer_cancel(backend->composer, backend->compose_frame);
        retire_frame(backend);
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    if (!backend->has_edge) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    uint64_t edge = backend->latest_edge;
    pv2_lock_exit(&backend->lock, false);
    pv2_source_t source;
    uint64_t window = backend->config.te_window_us - backend->config.te_transfer_budget_us;
    esp_err_t error = pv2_te_composer_offer(backend->composer, backend->compose_frame,
                                            edge, pv2_now_us(), window, &source);
    if (error == ESP_ERR_TIMEOUT) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    uint64_t cookie = pv2_new_token();
    if (!cookie) {
        (void)pv2_te_composer_reject(backend->composer, backend->compose_frame);
        return ESP_ERR_NO_MEM;
    }
    pv2_lock_enter(&backend->lock, false);
    /* Tasks are serialized. A channel fault can arrive while offering. */
    if (backend->fault.result != ESP_OK) {
        (void)pv2_te_composer_reject(backend->composer, backend->compose_frame);
        (void)pv2_te_composer_cancel(backend->composer, backend->compose_frame);
        retire_frame(backend);
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    backend->cookie = cookie;
    backend->done = false;
    backend->phase = TE_SUBMITTING;
    pv2_lock_exit(&backend->lock, false);
    pv2_surface_t surface = {(void *)source.pixels, source.capacity_bytes, source.layout};
    pv2_rect_t area = {0, 0, source.layout.width, source.layout.height};
    pv2_tx_result_t tx = backend->config.panel.submit(backend->config.panel.context, &surface, &area, cookie);
    pv2_lock_enter(&backend->lock, false);
    if (tx.acceptance == PV2_TX_REJECTED && !backend->done) {
        backend->phase = TE_READY;
        backend->cookie = 0;
        (void)pv2_te_composer_reject(backend->composer, backend->compose_frame);
        pv2_lock_exit(&backend->lock, false);
        return tx.result == ESP_OK ? ESP_FAIL : tx.result;
    }
    if (tx.acceptance != PV2_TX_ACCEPTED || tx.result != ESP_OK) {
        record_fault(backend, tx.result == ESP_OK ? ESP_FAIL : tx.result);
    }
    backend->phase = TE_INFLIGHT;
    if (backend->done) {
        (void)pv2_te_composer_complete(backend->composer, backend->compose_frame,
                                       backend->records[backend->active].result);
        retire_frame(backend);
    }
    pv2_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static void free_backend(pv2_backend_t *backend)
{
    if (backend->tiles) {
        for (size_t i = 0; i < backend->config.buffer_count; i++) {
            pv2_free(backend->tiles[i].pixels);
        }
    }
    if (backend->composer) {
        (void)pv2_te_composer_stop(backend->composer, UINT32_MAX);
        (void)pv2_te_composer_destroy(backend->composer);
    }
    pv2_free(backend->tiles);
    pv2_free(backend->records);
    pv2_lock_destroy(&backend->lock);
    pv2_free(backend);
}

static esp_err_t te_create(const pv2_backend_config_t *config, pv2_backend_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->panel.attach || !config->panel.submit || !config->panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->copy_execute && !config->async_copy) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->width || !config->height || !config->tile_rows || config->tile_rows > config->height ||
            !config->buffer_count || config->buffer_count > TE_MAX_BUFFERS ||
            !config->max_frames || config->max_frames > TE_MAX_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((config->draw_order != PV2_ORDER_NATIVE && config->draw_order != PV2_ORDER_SWAPPED) ||
            (config->format == PV2_FORMAT_RGB888 && config->draw_order != PV2_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->render_contract != PV2_CONTRACT_PARTITION || (config->rotation < PV2_ROTATE_0 || config->rotation > PV2_ROTATE_270) || config->initial_scanout_buffer || config->external_buffers || config->external_buffer_bytes || config->in_place_convert) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->te_transfer_budget_us ||
            config->te_transfer_budget_us >= config->te_window_us) {
        return ESP_ERR_INVALID_ARG;
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
    backend->active = SIZE_MAX;
    bool quarter = config->rotation == PV2_ROTATE_90 || config->rotation == PV2_ROTATE_270;
    const pv2_te_composer_config_t compose_config = {
        .width = quarter ? config->height : config->width, .height = quarter ? config->width : config->height,
        .rotation = config->rotation,
        .format = config->format, .panel_order = config->panel_order,
        .pixel = {
            .max_jobs = config->buffer_count, .asynchronous = config->async_copy,
            .execute = config->copy_execute, .execute_user = config->copy_user,
            .on_event = config->async_copy ? copy_done : NULL, .user = backend
        },
    };
    esp_err_t error = pv2_te_composer_create(&compose_config, &backend->composer);
    if (error != ESP_OK) {
        free_backend(backend);
        return error;
    }
    size_t bpp = config->format == PV2_FORMAT_RGB565 ? 2 : 3;
    backend->tile_bytes = (size_t)config->width * config->tile_rows * bpp;
    backend->tiles = pv2_alloc(config->buffer_count * sizeof(*backend->tiles));
    backend->records = pv2_alloc(config->max_frames * sizeof(*backend->records));
    if (!backend->tiles || !backend->records) {
        free_backend(backend);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < config->buffer_count; i++) {
        backend->tiles[i].pixels = pv2_alloc_pixels(backend->tile_bytes);
        if (!backend->tiles[i].pixels) {
            free_backend(backend);
            return ESP_ERR_NO_MEM;
        }
    }
    error = config->panel.attach(config->panel.context, te_dispatch, backend);
    if (error != ESP_OK) {
        free_backend(backend);
        return error;
    }
    *out = backend;
    return ESP_OK;
}

static esp_err_t te_get_caps(pv2_backend_t *backend, pv2_backend_caps_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    *out = (pv2_backend_caps_t) {
        .contract = PV2_CONTRACT_PARTITION,
        .draw_layout = {backend->config.width, backend->config.height,
                        backend->config.width * bpp, backend->config.format, backend->config.draw_order
                       },
        .drawbuf_capacity_bytes = backend->tile_bytes, .buffer_alignment = PV2_BUFFER_ALIGNMENT,
        .max_tile_rows = backend->config.tile_rows, .max_damage_areas = TE_MAX_DAMAGE,
        .retains_content = true,
    };
    return ESP_OK;
}

static esp_err_t validate_damage(pv2_backend_t *backend, const pv2_rect_t *areas, size_t count)
{
    return pv2_validate_damage(backend->config.width, backend->config.height, areas, count);
}

static esp_err_t te_begin(pv2_backend_t *backend, const pv2_frame_request_t *request, pv2_frame_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    out->value = 0;
    if (!backend || !request) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = validate_damage(backend, request->damage, request->damage_count);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_te_service(backend);
    pv2_lock_enter(&backend->lock, false);
    if (backend->lifecycle != TE_RUNNING || backend->fault.result != ESP_OK) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (backend->phase != TE_IDLE) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < backend->config.max_frames; i++) {
        if (backend->records[i].token) {
            continue;
        }
        /* begin allocates a token and clears a full coverage bitmap; outside lock. */
        pv2_lock_exit(&backend->lock, false);
        error = pv2_te_composer_begin(backend->composer, &backend->compose_frame);
        if (error != ESP_OK) {
            return error;
        }
        pv2_lock_enter(&backend->lock, false);
        if (backend->fault.result != ESP_OK) {
            (void)pv2_te_composer_cancel(backend->composer, backend->compose_frame);
            pv2_lock_exit(&backend->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        backend->records[i] = (te_record_t) {
            .token = backend->compose_frame.value
        };
        backend->active = i;
        backend->phase = TE_BUILDING;
        out->value = backend->compose_frame.value;
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    pv2_lock_exit(&backend->lock, false);
    return ESP_ERR_NO_MEM;
}

static bool building(pv2_backend_t *backend, pv2_frame_t frame)
{
    return backend->lifecycle == TE_RUNNING && backend->phase == TE_BUILDING &&
           frame.value && backend->records[backend->active].token == frame.value;
}

static esp_err_t te_acquire(pv2_backend_t *backend, pv2_frame_t frame, const pv2_rect_t *area,
                            uint32_t timeout, pv2_lease_t *out)
{
    (void)timeout; /* Presenter performs bounded event waits and retries. */
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_lease_t) {
        0
    };
    pv2_rect_t actual = area ? *area : (pv2_rect_t) {
        0, 0, backend->config.width, backend->config.tile_rows
    };
    if (!valid_area(backend, &actual)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (actual.height > backend->config.tile_rows) {
        actual.height = backend->config.tile_rows;
    }
    (void)progress_copies(backend);
    pv2_lock_enter(&backend->lock, false);
    bool allowed = building(backend, frame) && backend->fault.result == ESP_OK;
    pv2_lock_exit(&backend->lock, false);
    if (!allowed) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        te_tile_t *tile = &backend->tiles[i];
        if (tile->lease.id.value || tile->copy.value) {
            continue;
        }
        uint64_t token = pv2_new_token();
        if (!token) {
            return ESP_ERR_NO_MEM;
        }
        size_t bpp = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
        tile->lease = (pv2_lease_t) {
            .id = {token}, .logical_area = actual,
            .surface = {tile->pixels, backend->tile_bytes,
                {
                    actual.width, actual.height, actual.width * bpp,
                    backend->config.format, backend->config.draw_order
                }
            }
        };
        *out = tile->lease;
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

static te_tile_t *find_lease(pv2_backend_t *backend, pv2_lease_id_t lease)
{
    for (size_t i = 0; lease.value && i < backend->config.buffer_count; i++) {
        if (backend->tiles[i].lease.id.value == lease.value) {
            return &backend->tiles[i];
        }
    }
    return NULL;
}

static esp_err_t te_submit_region_ex(pv2_backend_t *backend, pv2_frame_t frame,
                                     pv2_lease_id_t lease, const pv2_region_submit_t *submit)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    bool allowed = building(backend, frame) && backend->fault.result == ESP_OK;
    pv2_lock_exit(&backend->lock, false);
    te_tile_t *tile = find_lease(backend, lease);
    if (!allowed || !tile) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_rect_t area = tile->lease.logical_area;
    if (submit && (submit->logical_area.x || submit->logical_area.y ||
                   submit->logical_area.width || submit->logical_area.height)) {
        area = submit->logical_area;
    }
    if (!valid_area(backend, &area)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t stride = submit && submit->stride_bytes ? submit->stride_bytes : area.width * bpp;
    pv2_source_t source = {tile->pixels, backend->tile_bytes,
        {area.width, area.height, stride, backend->config.format, backend->config.draw_order}
    };
    pv2_rect_t physical;
    esp_err_t mapped = pv2_rotate_rect(backend->config.width, backend->config.height, backend->config.rotation, &area, &physical);
    if (mapped != ESP_OK) {
        return mapped;
    }
    pv2_rect_t source_area = {0, 0, area.width, area.height};
    esp_err_t error;
    if (backend->config.async_copy) {
        error = pv2_te_composer_queue_write(backend->composer, backend->compose_frame,
                                            &source, &source_area, physical.x, physical.y, &tile->copy);
    } else {
        error = pv2_te_composer_write(backend->composer, backend->compose_frame,
                                      &source, &source_area, physical.x, physical.y);
    }
    if (error == ESP_OK) {
        tile->lease.id.value = 0;
    }
    return error;
}

static esp_err_t te_submit_region(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease)
{
    return te_submit_region_ex(backend, frame, lease, NULL);
}

static esp_err_t te_cancel_region(pv2_backend_t *backend, pv2_frame_t frame, pv2_lease_id_t lease)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    te_tile_t *tile = find_lease(backend, lease);
    if (!building(backend, frame) || !tile) {
        return ESP_ERR_INVALID_STATE;
    }
    tile->lease.id.value = 0;
    return ESP_OK;
}

static esp_err_t te_get_draw_buffers(pv2_backend_t *backend, void **buffers, size_t capacity,
                                     size_t *count, size_t *bytes)
{
    if (!backend || !count || (capacity && !buffers)) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = backend->config.buffer_count;
    if (bytes) {
        *bytes = backend->tile_bytes;
    }
    if (capacity < *count) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < *count; i++) {
        buffers[i] = backend->tiles[i].pixels;
    }
    return ESP_OK;
}

static esp_err_t te_end_ex(pv2_backend_t *backend, pv2_frame_t frame,
                           const pv2_frame_coverage_t *coverage, pv2_receipt_t *out)
{
    if (out) {
        out->value = 0;
    }
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    if (coverage) {
        esp_err_t error = validate_damage(backend, coverage->areas, coverage->area_count);
        if (error != ESP_OK) {
            return error;
        }
    }
    if (!building(backend, frame)) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        if (backend->tiles[i].lease.id.value) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    size_t pending = progress_copies(backend);
    /* Successful copies, never damage hints, establish coverage. */
    esp_err_t error = pending ? ESP_OK :
                      pv2_te_composer_seal(backend->composer, backend->compose_frame, pv2_now_us());
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
        return error;
    }
    pv2_lock_enter(&backend->lock, false);
    backend->phase = (pending || error == ESP_ERR_TIMEOUT) ? TE_COPY_DRAIN : TE_READY;
    backend->has_edge = false; /* Edges before repair completes are not credits. */
    if (out) {
        backend->records[backend->active].external = true;
        out->value = frame.value;
    }
    pv2_lock_exit(&backend->lock, false);
    /* end consumes frame regardless of later panel acceptance; service reports
     * definite rejection separately, never returns an error implying end rollback. */
    return ESP_OK;
}

static esp_err_t te_end(pv2_backend_t *backend, pv2_frame_t frame, pv2_receipt_t *out)
{
    return te_end_ex(backend, frame, NULL, out);
}

static esp_err_t te_cancel_frame(pv2_backend_t *backend, pv2_frame_t frame, uint32_t timeout)
{
    if (!backend || !frame.value) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pv2_cancel_matches(&backend->canceled, frame.value)) {
        return ESP_OK;
    }
    if (!building(backend, frame) &&
            !(backend->phase == TE_CANCELING && backend->compose_frame.value == frame.value)) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_lock_enter(&backend->lock, false);
    backend->phase = TE_CANCELING;
    pv2_lock_exit(&backend->lock, false);
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        backend->tiles[i].lease.id.value = 0;
    }
    uint64_t start = pv2_now_ms();
    do {
        (void)pv2_te_service(backend);
        if (pv2_cancel_matches(&backend->canceled, frame.value)) {
            return ESP_OK;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    } while (true);
}

static te_record_t *find_receipt(pv2_backend_t *backend, pv2_receipt_t receipt)
{
    return pv2_record_find(backend->records, backend->config.max_frames, sizeof(*backend->records), receipt.value, true);
}

static esp_err_t te_query(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_te_service(backend);
    pv2_lock_enter(&backend->lock, false);
    te_record_t *record = find_receipt(backend, receipt);
    if (!record) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    *out = pv2_record_status(record, true, record->terminal, record->terminal, false);
    pv2_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t te_get_fault(pv2_backend_t *backend, pv2_backend_fault_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    *out = backend->fault;
    pv2_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t te_wait(pv2_backend_t *backend, pv2_receipt_t receipt, pv2_completion_point_t point,
                         uint32_t timeout, pv2_frame_status_t *out)
{
    if (!backend || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (point != PV2_COMPLETION_SOURCE_RELEASED) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    do {
        esp_err_t error = te_query(backend, receipt, out);
        if (error != ESP_OK || out->terminal) {
            return error;
        }
        if (pv2_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_pause();
    } while (true);
}

static esp_err_t te_release_receipt(pv2_backend_t *backend, pv2_receipt_t receipt)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_te_service(backend);
    pv2_lock_enter(&backend->lock, false);
    te_record_t *record = find_receipt(backend, receipt);
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (record && record->terminal) {
        record->token = 0;
        error = ESP_OK;
    }
    pv2_lock_exit(&backend->lock, false);
    return error;
}

static esp_err_t te_stop(pv2_backend_t *backend, uint32_t timeout)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_lock_enter(&backend->lock, false);
    if (backend->lifecycle == TE_QUIESCENT) {
        pv2_lock_exit(&backend->lock, false);
        return ESP_OK;
    }
    backend->lifecycle = TE_CLOSING;
    pv2_lock_exit(&backend->lock, false);
    uint64_t start = pv2_now_ms();
    esp_err_t error = pv2_te_composer_stop(backend->composer, timeout);
    if (error != ESP_OK) {
        return error;
    }
    (void)progress_copies(backend);
    uint32_t left = pv2_remaining_ms(start, timeout);
    /* Copy callbacks have exited. Adapter joins panel callbacks separately. */
    error = backend->config.panel.quiesce(backend->config.panel.context, left);
    if (error != ESP_OK) {
        return error;
    }
    pv2_lock_enter(&backend->lock, false);
    if (backend->phase != TE_IDLE) {
        if (backend->phase == TE_INFLIGHT) {
            if (!backend->done) {
                record_fault(backend, ESP_ERR_INVALID_STATE);
            }
            (void)pv2_te_composer_complete(backend->composer, backend->compose_frame,
                                           backend->records[backend->active].result);
        } else {
            record_fault(backend, ESP_ERR_INVALID_STATE);
            (void)pv2_te_composer_cancel(backend->composer, backend->compose_frame);
        }
        retire_frame(backend);
    }
    for (size_t i = 0; i < backend->config.buffer_count; i++) {
        backend->tiles[i].lease.id.value = 0;
    }
    backend->lifecycle = TE_QUIESCENT;
    pv2_lock_exit(&backend->lock, false);
    return ESP_OK;
}

static esp_err_t te_destroy(pv2_backend_t *backend)
{
    if (!backend) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->lifecycle != TE_QUIESCENT) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.max_frames; i++) {
        if (backend->records[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    free_backend(backend);
    return ESP_OK;
}

static esp_err_t te_set_rotation(pv2_backend_t *backend, pv2_rotation_t rotation)
{
    if (!backend || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (backend->lifecycle != TE_RUNNING || backend->active != SIZE_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < backend->config.max_frames; ++i)
        if (backend->records[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    if (backend->config.in_place_convert && rotation != PV2_ROTATE_0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool change_axes = ((unsigned)backend->config.rotation ^ (unsigned)rotation) & 1;
    uint32_t width = change_axes ? backend->config.height : backend->config.width;
    uint32_t height = change_axes ? backend->config.width : backend->config.height;
    size_t bpp = backend->config.format == PV2_FORMAT_RGB565 ? 2 : 3;
    size_t rows = backend->tile_bytes / ((size_t)width * bpp);
    if (!rows) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = pv2_te_composer_set_rotation(backend->composer, rotation);
    if (error != ESP_OK) {
        return error;
    }
    backend->config.width = width;
    backend->config.height = height;
    backend->config.tile_rows = rows > height ? height : rows;
    backend->config.rotation = rotation;
    return ESP_OK;
}

const pv2_backend_ops_t pv2_te_backend = {
    .name = "te",
    .create = te_create,
    .get_caps = te_get_caps,
    .begin = te_begin,
    .acquire = te_acquire,
    .get_draw_buffers = te_get_draw_buffers,
    .submit_region = te_submit_region,
    .submit_region_ex = te_submit_region_ex,
    .cancel_region = te_cancel_region,
    .end = te_end,
    .end_ex = te_end_ex,
    .cancel_frame = te_cancel_frame,
    .query = te_query,
    .get_fault = te_get_fault,
    .wait = te_wait,
    .release_receipt = te_release_receipt,
    .on_event_isr = te_on_event_isr,
    .on_event = te_on_event,
    .stop = te_stop,
    .destroy = te_destroy,
    .set_rotation = te_set_rotation,
};
