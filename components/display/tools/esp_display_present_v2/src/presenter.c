/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_presenter.h"
#include "present_os.h"
#include "present_limits.h"
#include <limits.h>

typedef struct {
    uint64_t token;
    pv2_receipt_t backend;
    pv2_frame_status_t status;
} observer_t;

struct pv2_presenter {
    pv2_backend_t *backend;
    pv2_mutex_t *gate;
    pv2_event_t *changed;
    pv2_event_t *work;
    pv2_worker_t *worker;
    pv2_lock_t control;
    bool exit_requested;
    pv2_panel_adapter_t panel;
    pv2_progress_fn user_progress;
    void *progress_user;
    pv2_panel_event_fn backend_event;
    void *backend_event_user;
    esp_err_t last_service_error;
    const pv2_backend_ops_t *ops;
    pv2_presenter_backend_t kind;
    pv2_rotation_t rotation;
    bool (*event_isr)(pv2_backend_t *, const pv2_backend_event_t *);
    pv2_backend_caps_t caps;
    pv2_task_t owner;
    pv2_frame_t frame;
    pv2_lease_t held;
    pv2_receipt_t receipts[PV2_MAX_FRAMES];
    observer_t observers[PV2_MAX_FRAMES];
    size_t head, count, limit;
    uint32_t timeout;
    bool canceling;
    pv2_presenter_state_t state;
    uint64_t committed, completed;
    esp_err_t first_error;
};

static bool PV2_ISR_ATTR notify(pv2_presenter_t *p, bool isr)
{
    bool yield = pv2_event_signal(p->changed, isr);
    yield |= pv2_event_signal(p->work, isr);
    return yield;
}

static bool copy_progress(void *user, bool isr)
{
    pv2_presenter_t *p = user;
    bool yield = notify(p, isr);
    if (p->user_progress) {
        yield |= p->user_progress(p->progress_user, isr);
    }
    return yield;
}

static bool PV2_ISR_ATTR panel_event(void *context, const pv2_backend_event_t *event, bool isr)
{
    pv2_presenter_t *p = context;
    bool yield = p->backend_event(p->backend_event_user, event, isr);
    yield |= notify(p, isr);
    return yield;
}

static esp_err_t panel_attach(void *context, pv2_panel_event_fn callback, void *user)
{
    pv2_presenter_t *p = context;
    p->backend_event = callback;
    p->backend_event_user = user;
    return p->panel.attach(p->panel.context, panel_event, p);
}

static pv2_tx_result_t panel_submit(void *context, const pv2_surface_t *surface,
                                    const pv2_rect_t *area, uint64_t cookie)
{
    pv2_presenter_t *p = context;
    return p->panel.submit(p->panel.context, surface, area, cookie);
}

static esp_err_t panel_quiesce(void *context, uint32_t timeout)
{
    pv2_presenter_t *p = context;
    /* Underlying quiesce joins the entire panel_event callback, including notify. */
    return p->panel.quiesce(p->panel.context, timeout);
}

static bool exiting(pv2_presenter_t *p)
{
    pv2_lock_enter(&p->control, false);
    bool exit = p->exit_requested;
    pv2_lock_exit(&p->control, false);
    return exit;
}

static void request_exit(pv2_presenter_t *p)
{
    pv2_lock_enter(&p->control, false);
    p->exit_requested = true;
    pv2_lock_exit(&p->control, false);
    (void)pv2_event_signal(p->work, false);
}

static esp_err_t pump_backend(pv2_presenter_t *p)
{
    return p->kind == PV2_PRESENTER_TE ? pv2_te_service(p->backend) :
           p->kind == PV2_PRESENTER_FB ? pv2_fb_service(p->backend) : pv2_gram_service(p->backend);
}

static void sender(void *context)
{
    pv2_presenter_t *p = context;
    for (;;) {
        (void)pv2_event_wait(p->work, UINT32_MAX);
        if (exiting(p)) {
            return;
        }
        (void)pv2_mutex_take(p->gate, UINT32_MAX);
        if (exiting(p)) {
            pv2_mutex_give(p->gate);
            return;
        }
        p->last_service_error = pump_backend(p);
        pv2_mutex_give(p->gate);
        /* Wake owner only; signaling work here would cause a busy loop. */
        (void)pv2_event_signal(p->changed, false);
    }
}

static void free_presenter(pv2_presenter_t *p)
{
    pv2_worker_destroy(p->worker);
    pv2_event_destroy(p->work);
    pv2_event_destroy(p->changed);
    pv2_mutex_destroy(p->gate);
    pv2_lock_destroy(&p->control);
    pv2_free(p);
}

static esp_err_t check_owner(pv2_presenter_t *p)
{
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    return pv2_same_task(p->owner, pv2_current_task()) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static uint32_t remaining(uint64_t start, uint32_t timeout)
{
    if (timeout == UINT32_MAX) {
        return timeout;
    }
    uint64_t elapsed = pv2_now_ms() - start;
    return elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
}

static void record_error(pv2_presenter_t *p, esp_err_t error)
{
    if (p->first_error == ESP_OK) {
        p->first_error = error;
    }
}

static esp_err_t refresh_fault(pv2_presenter_t *p)
{
    pv2_backend_fault_t fault;
    esp_err_t ret = p->ops->get_fault(p->backend, &fault);
    if (ret == ESP_OK) {
        record_error(p, fault.result);
    }
    return ret;
}

static esp_err_t reap_ready(pv2_presenter_t *p)
{
    while (p->count) {
        pv2_frame_status_t status;
        esp_err_t ret = p->ops->query(p->backend, p->receipts[p->head], &status);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!status.terminal) {
            break;
        }
        ret = p->ops->release_receipt(p->backend, p->receipts[p->head]);
        if (ret != ESP_OK) {
            return ret;
        }
        for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
            observer_t *o = &p->observers[i];
            if (o->token && o->backend.value == p->receipts[p->head].value) {
                o->status = status;
            }
        }
        record_error(p, status.result);
        p->head = (p->head + 1) % p->limit;
        --p->count;
        ++p->completed;
    }
    return refresh_fault(p);
}

/* Called with gate held; only owner waits here. Worker has a separate event,
 * so it cannot consume the owner's notification. Always recheck after wake. */
static esp_err_t wait_change(pv2_presenter_t *p, uint64_t start, uint32_t timeout)
{
    uint32_t budget = remaining(start, timeout);
    if (!budget) {
        return ESP_ERR_TIMEOUT;
    }
    pv2_mutex_give(p->gate);
    bool signaled = pv2_event_wait(p->changed, budget);
    /* Reacquire before returning even on timeout: the caller must not inspect
     * backend state concurrently. Latency of an already running driver call
     * is not a hard real-time bound; adapter submit must return promptly. */
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    return signaled ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t wait_receipt(pv2_presenter_t *p, pv2_receipt_t receipt, uint32_t timeout,
                              pv2_frame_status_t *status)
{
    uint64_t start = pv2_now_ms();
    for (;;) {
        esp_err_t ret = p->ops->query(p->backend, receipt, status);
        if (ret != ESP_OK || status->terminal) {
            return ret;
        }
        ret = wait_change(p, start, timeout);
        if (ret != ESP_OK) {
            return ret;
        }
    }
}

static bool valid_lease(pv2_presenter_t *p, const pv2_lease_t *lease)
{
    return lease && p->held.id.value && lease->id.value == p->held.id.value &&
           lease->surface.pixels == p->held.surface.pixels &&
           lease->surface.capacity_bytes == p->held.surface.capacity_bytes;
}

static esp_err_t resolve_config(const pv2_presenter_config_t *input, pv2_presenter_config_t *out)
{
    if (!input || input->backend_kind < PV2_PRESENTER_GRAM || input->backend_kind > PV2_PRESENTER_FB ||
            input->mode < PV2_MODE_DEFAULT || input->mode > PV2_MODE_AUTO) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = *input;
    pv2_present_mode_t mode = input->mode;
    if (mode == PV2_MODE_AUTO) {
        if (input->backend_kind == PV2_PRESENTER_FB) {
            if (input->backend.buffer_count == 1) {
                mode = PV2_MODE_NONE;
            } else if (input->backend.buffer_count == 2) {
                mode = PV2_MODE_DOUBLE_PARTIAL;
            } else if (input->backend.buffer_count == 3) {
                mode = PV2_MODE_TRIPLE_PARTIAL;
            } else {
                return ESP_ERR_INVALID_ARG;
            }
        } else {
            mode = input->backend.te_window_us && input->backend.te_transfer_budget_us ? PV2_MODE_TE_SYNC : PV2_MODE_NONE;
        }
    }
    if (mode == PV2_MODE_DEFAULT) {
        return ESP_OK;
    }
    if (mode == PV2_MODE_NONE) {
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        if (input->backend_kind == PV2_PRESENTER_FB) {
            if (input->backend.external_buffers && input->backend.buffer_count != 1) {
                return ESP_ERR_INVALID_ARG;
            }
            out->backend.buffer_count = 1;
        } else {
            out->backend_kind = PV2_PRESENTER_GRAM;
        }
        return ESP_OK;
    }
    if (mode == PV2_MODE_TE_SYNC) {
        if (input->backend_kind == PV2_PRESENTER_FB) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        out->backend_kind = PV2_PRESENTER_TE;
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        return ESP_OK;
    }
    if (input->backend_kind != PV2_PRESENTER_FB) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    out->backend.buffer_count = mode == PV2_MODE_TRIPLE_FULL || mode == PV2_MODE_TRIPLE_PARTIAL ? 3 : 2;
    if (input->backend.external_buffers && input->backend.buffer_count != out->backend.buffer_count) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (mode) {
    case PV2_MODE_DOUBLE_DIRECT:
        out->backend.render_contract = PV2_CONTRACT_DIRECT;
        break;
    case PV2_MODE_DOUBLE_FULL:
    case PV2_MODE_TRIPLE_FULL:
        out->backend.render_contract = PV2_CONTRACT_FULL;
        break;
    default:
        out->backend.render_contract = PV2_CONTRACT_PARTITION;
        break;
    }
    return ESP_OK;
}

esp_err_t pv2_presenter_get_required_frame_buffer_count(const pv2_presenter_config_t *config, size_t *count)
{
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = 0;
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_config_t input = *config, resolved;
    input.backend.external_buffers = NULL;
    esp_err_t error = resolve_config(&input, &resolved);
    if (error != ESP_OK) {
        return error;
    }
    if (resolved.backend.rotation < PV2_ROTATE_0 || resolved.backend.rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (resolved.backend_kind == PV2_PRESENTER_FB) {
        if (!resolved.backend.buffer_count || resolved.backend.buffer_count > 3) {
            return ESP_ERR_INVALID_ARG;
        }
        *count = resolved.backend.buffer_count;
    }
    return ESP_OK;
}

esp_err_t pv2_presenter_rebind_producer(pv2_presenter_t *p, uint32_t timeout)
{
    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pv2_mutex_take(p->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (p->state == PV2_PRESENTER_RUNNING && !p->frame.value && !p->held.id.value && !p->canceling) {
        p->owner = pv2_current_task();
        error = ESP_OK;
    }
    pv2_mutex_give(p->gate);
    return error;
}

esp_err_t pv2_presenter_get_draw_buffers(pv2_presenter_t *p, void **buffers,
                                         size_t capacity, size_t *count, size_t *bytes)
{
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    error = p->ops->get_draw_buffers(p->backend, buffers, capacity, count, bytes);
    pv2_mutex_give(p->gate);
    return error;
}

esp_err_t pv2_presenter_create(const pv2_presenter_config_t *config, pv2_presenter_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    pv2_presenter_config_t resolved;
    esp_err_t resolution = resolve_config(config, &resolved);
    if (resolution != ESP_OK) {
        return resolution;
    }
    config = &resolved;
    if (config->backend.width > INT_MAX || config->backend.height > INT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->background_service && config->backend_kind == PV2_PRESENTER_GRAM && !config->backend.async_copy) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!config->backend.panel.attach || !config->backend.panel.submit || !config->backend.panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_t *p = pv2_alloc(sizeof(*p));
    if (!p) {
        return ESP_ERR_NO_MEM;
    }
    if (!pv2_lock_init(&p->control)) {
        pv2_free(p);
        return ESP_ERR_NO_MEM;
    }
    p->gate = pv2_mutex_create();
    p->changed = pv2_event_create();
    p->work = pv2_event_create();
    if (!p->gate || !p->changed || !p->work) {
        free_presenter(p);
        return ESP_ERR_NO_MEM;
    }
    p->kind = config->backend_kind;
    p->rotation = config->backend.rotation;
    p->ops = p->kind == PV2_PRESENTER_TE ? &pv2_te_backend :
             p->kind == PV2_PRESENTER_FB ? &pv2_fb_backend : &pv2_gram_backend;
    p->event_isr = p->ops->on_event_isr;
    p->owner = pv2_current_task();
    p->timeout = config->timeout_ms;
    p->limit = config->backend.max_frames;
    p->panel = config->backend.panel;
    p->user_progress = config->backend.on_progress;
    p->progress_user = config->backend.progress_user;
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    if (config->background_service) {
        p->worker = pv2_worker_create(sender, p, config->worker_stack_bytes ? config->worker_stack_bytes : 4096,
                                      config->worker_priority ? config->worker_priority : 5);
        if (!p->worker) {
            pv2_mutex_give(p->gate);
            free_presenter(p);
            return ESP_ERR_NO_MEM;
        }
    }
    pv2_backend_config_t backend = config->backend;
    backend.panel = (pv2_panel_adapter_t) {
        p, panel_attach, panel_submit, panel_quiesce
    };
    backend.on_progress = copy_progress;
    backend.progress_user = p;
    esp_err_t ret = p->ops->create(&backend, &p->backend);
    if (ret != ESP_OK) {
        request_exit(p);
        pv2_mutex_give(p->gate);
        if (p->worker) {
            (void)pv2_worker_join(p->worker, UINT32_MAX);
        }
        free_presenter(p);
        return ret;
    }
    (void)p->ops->get_caps(p->backend, &p->caps);
    pv2_mutex_give(p->gate);
    *out = p;
    return ESP_OK;
}

static esp_err_t service_impl(pv2_presenter_t *p)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_err_t service = pump_backend(p);
    p->last_service_error = service;
    ret = reap_ready(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (p->first_error != ESP_OK) {
        return p->first_error;
    }
    return service;
}

void pv2_presenter_notify_te(pv2_presenter_t *p, uint64_t timestamp_us)
{
    if (!p || p->kind != PV2_PRESENTER_TE) {
        return;
    }
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp_us};
    pv2_te_backend.on_event(p->backend, &event);
    (void)notify(p, false);
}

bool PV2_ISR_ATTR pv2_presenter_notify_te_isr(pv2_presenter_t *p, uint64_t timestamp_us)
{
    if (!p || p->kind != PV2_PRESENTER_TE) {
        return false;
    }
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp_us};
    bool yield = p->event_isr(p->backend, &event);
    yield |= notify(p, true);
    return yield;
}

static esp_err_t get_caps_impl(pv2_presenter_t *p, pv2_backend_caps_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = p->caps;
    return ESP_OK;
}

static esp_err_t begin_next_frame_impl(pv2_presenter_t *p, const pv2_frame_request_t *request, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p->state != PV2_PRESENTER_RUNNING || p->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = reap_ready(p);
        if (ret != ESP_OK) {
            return ret;
        }
        if (p->first_error != ESP_OK) {
            return p->first_error;
        }
        if (p->count < p->limit) {
            ret = p->ops->begin(p->backend, request, &p->frame);
            if (ret != ESP_ERR_NO_MEM || !p->count) {
                return ret;
            }
        }
        /* Only an outstanding receipt can make admission progress. The same
         * deadline covers record pressure AND TE's single active candidate. */
        uint32_t budget = remaining(start, timeout);
        if (!budget) {
            return ESP_ERR_TIMEOUT;
        }
        pv2_frame_status_t status;
        ret = wait_receipt(p, p->receipts[p->head], budget, &status);
        if (ret != ESP_OK) {
            return ret;
        }
    }
}

static esp_err_t acquire_buffer_impl(pv2_presenter_t *p, pv2_lease_t *out, uint32_t timeout)
{
    if (out) {
        *out = (pv2_lease_t) {
            0
        };
    }
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p->state != PV2_PRESENTER_RUNNING || !p->frame.value || p->held.id.value || p->canceling) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = p->ops->acquire(p->backend, p->frame, NULL, 0, &p->held);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(p, start, timeout);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ret == ESP_OK) {
        *out = p->held;
    }
    return ret;
}

static esp_err_t resolve_rows_impl(pv2_presenter_t *p, const pv2_lease_t *lease,
                                   size_t stride, uint32_t requested, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out || !stride || !requested) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!valid_lease(p, lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bpp = p->caps.draw_layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (stride % bpp || stride > p->held.surface.capacity_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t rows = p->held.surface.capacity_bytes / stride;
    *out = rows < requested ? (uint32_t)rows : requested;
    return ESP_OK;
}

static esp_err_t submit_buffer_impl(pv2_presenter_t *p, const pv2_lease_t *lease,
                                    const pv2_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!area || !stride || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1 ||
            (uint32_t)area->x2 >= p->caps.draw_layout.width || (uint32_t)area->y2 >= p->caps.draw_layout.height) {
        return ESP_ERR_INVALID_ARG;
    }
    if (p->state != PV2_PRESENTER_RUNNING || p->canceling || !valid_lease(p, lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_region_submit_t submit = { .logical_area = { (uint32_t)area->x1, (uint32_t)area->y1,
                                                     (uint32_t)(area->x2 - area->x1) + 1, (uint32_t)(area->y2 - area->y1) + 1
                                                   }, .stride_bytes = stride
                                 };
    ret = p->ops->submit_region_ex(p->backend, p->frame, p->held.id, &submit);
    if (ret == ESP_OK) {
        p->held = (pv2_lease_t) {
            0
        };
    }
    return ret;
}

static esp_err_t commit_frame_impl(pv2_presenter_t *p, const pv2_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (p->state != PV2_PRESENTER_RUNNING || !p->frame.value || p->canceling || p->held.id.value || p->count == p->limit) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_receipt_t receipt;
    ret = p->ops->end_ex(p->backend, p->frame, coverage, &receipt);
    if (ret != ESP_OK) {
        return ret;
    }
    p->receipts[(p->head + p->count) % p->limit] = receipt;
    ++p->count;
    ++p->committed;
    p->frame.value = 0;
    (void)pv2_event_signal(p->work, false);
    return ESP_OK;
}

static esp_err_t cancel_frame_impl(pv2_presenter_t *p, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!p->frame.value) {
        return ESP_OK;
    }
    p->canceling = true;
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = p->ops->cancel_frame(p->backend, p->frame, 0);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(p, start, timeout);
        if (ret != ESP_OK) {
            break;
        }
    }
    p->held = (pv2_lease_t) {
        0
    };
    if (ret == ESP_OK) {
        p->frame.value = 0;
        p->canceling = false;
    }
    return ret;
}

static esp_err_t drain_impl(pv2_presenter_t *p, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (p->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    while (p->count) {
        pv2_frame_status_t status;
        ret = wait_receipt(p, p->receipts[p->head], remaining(start, timeout), &status);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = reap_ready(p);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    ret = refresh_fault(p);
    return ret != ESP_OK ? ret : p->first_error;
}

static esp_err_t get_status_impl(pv2_presenter_t *p, pv2_presenter_status_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = reap_ready(p);
    if (ret != ESP_OK) {
        return ret;
    }
    *out = (pv2_presenter_status_t) {
        .state = p->state, .frame_active = p->frame.value != 0,
        .cancel_pending = p->canceling, .pending_frames = p->count,
        .committed_frames = p->committed, .completed_frames = p->completed, .first_error = p->first_error, .last_service_error = p->last_service_error
    };
    return ESP_OK;
}

static esp_err_t stop_impl(pv2_presenter_t *p, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    p->state = PV2_PRESENTER_CLOSING;
    ret = p->ops->stop(p->backend, timeout);
    p->held = (pv2_lease_t) {
        0
    };
    if (ret != ESP_OK) {
        return ret;
    }
    p->frame.value = 0;
    p->canceling = false;
    ret = reap_ready(p);
    if (ret != ESP_OK) {
        return ret;
    }
    p->state = PV2_PRESENTER_STOPPED;
    return ESP_OK;
}

static esp_err_t destroy_impl(pv2_presenter_t *p)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (p->state != PV2_PRESENTER_STOPPED || p->count || p->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i)
        if (p->observers[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    ret = p->ops->destroy(p->backend);
    return ret;
}

esp_err_t pv2_presenter_service(pv2_presenter_t *p)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = service_impl(p);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_get_caps(pv2_presenter_t *p, pv2_backend_caps_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = get_caps_impl(p, out);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_begin_next_frame(pv2_presenter_t *p, const pv2_frame_request_t *request)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (p->state != PV2_PRESENTER_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pv2_mutex_take(p->gate, p->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = begin_next_frame_impl(p, request, remaining(start, p->timeout));
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_acquire_buffer(pv2_presenter_t *p, pv2_lease_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (p->state != PV2_PRESENTER_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pv2_mutex_take(p->gate, p->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = acquire_buffer_impl(p, out, remaining(start, p->timeout));
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_resolve_rows(pv2_presenter_t *p, const pv2_lease_t *lease, size_t stride, uint32_t requested, uint32_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = resolve_rows_impl(p, lease, stride, requested, out);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_submit_buffer(pv2_presenter_t *p, const pv2_lease_t *lease, const pv2_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = submit_buffer_impl(p, lease, area, stride);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_commit_frame(pv2_presenter_t *p, const pv2_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = commit_frame_impl(p, coverage);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_cancel_frame(pv2_presenter_t *p)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(p->gate, p->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = cancel_frame_impl(p, remaining(start, p->timeout));
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_drain(pv2_presenter_t *p, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(p->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = drain_impl(p, remaining(start, timeout));
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_get_status(pv2_presenter_t *p, pv2_presenter_status_t *out)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = get_status_impl(p, out);
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_stop(pv2_presenter_t *p, uint32_t timeout)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    /* Owner state is not accessed by sender. Once closing, no new UI frame
     * may be admitted even if sender join times out. */
    p->state = PV2_PRESENTER_CLOSING;
    request_exit(p);
    if (p->worker && !pv2_worker_join(p->worker, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!pv2_mutex_take(p->gate, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    ret = stop_impl(p, remaining(start, timeout));
    pv2_mutex_give(p->gate);
    return ret;
}

esp_err_t pv2_presenter_destroy(pv2_presenter_t *p)
{
    esp_err_t ret = check_owner(p);
    if (ret != ESP_OK) {
        return ret;
    }
    if (p->state != PV2_PRESENTER_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    ret = destroy_impl(p);
    pv2_mutex_give(p->gate);
    if (ret == ESP_OK) {
        free_presenter(p);
    }
    return ret;
}

static observer_t *find_observer(pv2_presenter_t *p, pv2_presenter_receipt_t receipt)
{
    for (size_t i = 0; receipt.value && i < PV2_MAX_FRAMES; ++i)
        if (p->observers[i].token == receipt.value) {
            return &p->observers[i];
        }
    return NULL;
}

esp_err_t pv2_presenter_commit_with_receipt(pv2_presenter_t *p, const pv2_frame_coverage_t *coverage,
                                            pv2_presenter_receipt_t *receipt)
{
    if (!receipt) {
        return ESP_ERR_INVALID_ARG;
    }
    receipt->value = 0;
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    observer_t *o = NULL;
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i)
        if (!p->observers[i].token) {
            o = &p->observers[i];
            break;
        }
    uint64_t token = o ? pv2_new_token() : 0;
    if (!token) {
        error = ESP_ERR_NO_MEM;
    } else {
        error = commit_frame_impl(p, coverage);
        if (error == ESP_OK) {
            *o = (observer_t) {
                .token = token, .backend = p->receipts[(p->head + p->count - 1) % p->limit],
                .status = {.sealed = true}
            };
            receipt->value = token;
        }
    }
    pv2_mutex_give(p->gate);
    return error;
}

static esp_err_t query_observer(pv2_presenter_t *p, observer_t *o, pv2_frame_status_t *out)
{
    if (!o) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!o->status.terminal) {
        esp_err_t error = p->ops->query(p->backend, o->backend, &o->status);
        if (error != ESP_OK) {
            return error;
        }
    }
    *out = o->status;
    return ESP_OK;
}

esp_err_t pv2_presenter_query_frame(pv2_presenter_t *p, pv2_presenter_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    error = query_observer(p, find_observer(p, receipt), out);
    pv2_mutex_give(p->gate);
    return error;
}

esp_err_t pv2_presenter_wait_frame(pv2_presenter_t *p, pv2_presenter_receipt_t receipt,
                                   pv2_completion_point_t point, uint32_t timeout, pv2_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    if (point != PV2_COMPLETION_SOURCE_RELEASED && point != PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == PV2_COMPLETION_PRESENT_CONFIRMED && !p->caps.supports_present_confirmation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(p->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    observer_t *o = find_observer(p, receipt);
    do {
        error = query_observer(p, o, out);
        if (error != ESP_OK || out->terminal) {
            break;
        }
        error = wait_change(p, start, timeout);
    } while (error == ESP_OK);
    pv2_mutex_give(p->gate);
    return error;
}

static esp_err_t untrack(pv2_presenter_t *p, pv2_presenter_receipt_t receipt, bool require_terminal)
{
    if (!receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    observer_t *o = find_observer(p, receipt);
    pv2_frame_status_t status;
    if (!o) {
        error = ESP_ERR_INVALID_STATE;
    } else if (require_terminal) {
        error = query_observer(p, o, &status);
        if (error == ESP_OK && !status.terminal) {
            error = ESP_ERR_INVALID_STATE;
        }
    }
    if (error == ESP_OK) {
        o->token = 0;
    }
    pv2_mutex_give(p->gate);
    return error;
}

esp_err_t pv2_presenter_release_receipt(pv2_presenter_t *p, pv2_presenter_receipt_t receipt)
{
    return untrack(p, receipt, true);
}

esp_err_t pv2_presenter_cancel_tracking(pv2_presenter_t *p, pv2_presenter_receipt_t receipt)
{
    return untrack(p, receipt, false);
}

esp_err_t pv2_presenter_set_rotation(pv2_presenter_t *p, pv2_rotation_t rotation, uint32_t timeout)
{
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    if (rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!p->ops->set_rotation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(p->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    if (p->state != PV2_PRESENTER_RUNNING || p->frame.value || p->held.id.value) {
        error = ESP_ERR_INVALID_STATE;
    } else {
        error = drain_impl(p, remaining(start, timeout));
    }
    if (error == ESP_OK) {
        error = p->ops->set_rotation(p->backend, rotation);
    }
    if (error == ESP_OK) {
        p->rotation = rotation;
        error = p->ops->get_caps(p->backend, &p->caps);
    }
    pv2_mutex_give(p->gate);
    return error;
}

esp_err_t pv2_presenter_map_point(pv2_presenter_t *p, uint32_t x, uint32_t y, uint32_t *lx, uint32_t *ly)
{
    esp_err_t error = check_owner(p);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(p->gate, UINT32_MAX);
    error = pv2_unrotate_point(p->caps.draw_layout.width, p->caps.draw_layout.height, p->rotation, x, y, lx, ly);
    pv2_mutex_give(p->gate);
    return error;
}
