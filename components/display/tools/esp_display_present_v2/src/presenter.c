/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "present_presenter.h"
#include "present_config.h"
#include "present_plan.h"
#include "present_os.h"
#include "present_limits.h"
#include <limits.h>
#include <stdatomic.h>
#include "presenter_control.h"

typedef struct {
    uint64_t token;
    pv2_receipt_t backend;
    pv2_frame_status_t status;
} observer_t;

/* Timing evidence only. Backend/adapter retain all pixel ownership. */
typedef struct {
    uint64_t cookie, started_us;
    bool reported;
} transfer_watch_t;

struct pv2_presenter {
    pv2_backend_t *backend;
    pv2_mutex_t *gate;
    pv2_event_t *changed;
    pv2_event_t *work;
    pv2_worker_t *worker;
    pv2_lock_t control;
    uint64_t transfer_timeout_us; /* Immutable before callback registration. */
    transfer_watch_t *transfers; /* Allocated only when the deadline policy is enabled. */
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
    pv2_output_caps_t output;
    pv2_task_t owner;
    pv2_frame_t frame;
    pv2_lease_t held;
    pv2_receipt_t receipts[PV2_MAX_FRAMES];
    observer_t observers[PV2_MAX_FRAMES];
    size_t head, count, limit;
    uint32_t timeout;
    bool canceling;
    _Atomic pv2_presenter_state_t state;
    uint64_t committed, completed;
    esp_err_t first_error;
};

static bool PV2_ISR_ATTR notify(pv2_presenter_t *presenter, bool isr)
{
    bool yield = pv2_event_signal(presenter->changed, isr);
    yield |= pv2_event_signal(presenter->work, isr);
    return yield;
}

/* The control lock protects timing stamps; never call the backend under it.
 * A completion which races the task timeout reports FAULT before the genuine
 * DONE itself, so a successful receipt cannot escape ahead of the timeout. */
static bool PV2_ISR_ATTR finish_transfer(pv2_presenter_t *presenter, const pv2_backend_event_t *event, bool isr)
{
    if (!presenter->transfer_timeout_us ||
            (event->kind != PV2_EVENT_TRANSFER_DONE && event->kind != PV2_EVENT_SCANOUT_CONFIRMED)) {
        return false;
    }
    uint64_t now = pv2_now_us();
    bool expired = false;
    pv2_lock_enter(&presenter->control, isr);
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
        transfer_watch_t *watch = &presenter->transfers[i];
        if (watch->cookie && watch->cookie == event->transaction_cookie) {
            expired = now - watch->started_us >= presenter->transfer_timeout_us;
            watch->cookie = 0;
            break;
        }
    }
    pv2_lock_exit(&presenter->control, isr);
    if (!expired) {
        return false;
    }
    const pv2_backend_event_t failure = {.kind = PV2_EVENT_FAULT,
                                         .transaction_cookie = event->transaction_cookie, .timestamp_us = now, .result = ESP_ERR_TIMEOUT
                                        };
    return presenter->backend_event(presenter->backend_event_user, &failure, isr);
}

/* Task-only, presenter gate held: submissions cannot start during the scan. */
static void expire_transfers(pv2_presenter_t *presenter)
{
    if (!presenter->transfer_timeout_us) {
        return;
    }
    uint64_t expired[PV2_MAX_FRAMES];
    size_t count = 0;
    uint64_t now = pv2_now_us();
    pv2_lock_enter(&presenter->control, false);
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
        transfer_watch_t *watch = &presenter->transfers[i];
        if (watch->cookie && !watch->reported && now - watch->started_us >= presenter->transfer_timeout_us) {
            watch->reported = true;
            expired[count++] = watch->cookie;
        }
    }
    pv2_lock_exit(&presenter->control, false);
    for (size_t i = 0; i < count; ++i) {
        const pv2_backend_event_t event = {.kind = PV2_EVENT_FAULT,
                                           .transaction_cookie = expired[i], .timestamp_us = now, .result = ESP_ERR_TIMEOUT
                                          };
        (void)presenter->backend_event(presenter->backend_event_user, &event, false);
    }
    if (count) {
        (void)notify(presenter, false);
    }
}

static uint32_t transfer_wait_ms(pv2_presenter_t *presenter)
{
    if (!presenter->transfer_timeout_us) {
        return UINT32_MAX;
    }
    uint64_t remaining_us = UINT64_MAX, now = pv2_now_us();
    pv2_lock_enter(&presenter->control, false);
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
        transfer_watch_t *watch = &presenter->transfers[i];
        if (!watch->cookie || watch->reported) {
            continue;
        }
        /* Sender computes its next wait outside gate; a producer may have
         * inserted a newer stamp after this clock snapshot. */
        uint64_t elapsed = now >= watch->started_us ? now - watch->started_us : 0;
        uint64_t left = elapsed >= presenter->transfer_timeout_us ? 0 : presenter->transfer_timeout_us - elapsed;
        if (left < remaining_us) {
            remaining_us = left;
        }
    }
    pv2_lock_exit(&presenter->control, false);
    if (remaining_us == UINT64_MAX) {
        return UINT32_MAX;
    }
    uint64_t ms = (remaining_us + 999) / 1000;
    return ms >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)ms;
}

static bool copy_progress(void *user, bool isr)
{
    pv2_presenter_t *presenter = user;
    bool yield = notify(presenter, isr);
    if (presenter->user_progress) {
        yield |= presenter->user_progress(presenter->progress_user, isr);
    }
    return yield;
}

static bool PV2_ISR_ATTR panel_event(void *context, const pv2_backend_event_t *event, bool isr)
{
    pv2_presenter_t *presenter = context;
    bool yield = finish_transfer(presenter, event, isr);
    yield |= presenter->backend_event(presenter->backend_event_user, event, isr);
    yield |= notify(presenter, isr);
    return yield;
}

static esp_err_t panel_attach(void *context, pv2_panel_event_fn callback, void *user)
{
    pv2_presenter_t *presenter = context;
    presenter->backend_event = callback;
    presenter->backend_event_user = user;
    return presenter->panel.attach(presenter->panel.context, panel_event, presenter);
}

static pv2_tx_result_t panel_submit(void *context, const pv2_surface_t *surface,
                                    const pv2_rect_t *area, uint64_t cookie)
{
    pv2_presenter_t *presenter = context;
    if (area->x % presenter->output.x_alignment || area->y % presenter->output.y_alignment ||
            area->width % presenter->output.width_alignment || area->height % presenter->output.height_alignment)
        return (pv2_tx_result_t) {
        PV2_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    if (!presenter->transfer_timeout_us) {
        return presenter->panel.submit(presenter->panel.context, surface, area, cookie);
    }
    expire_transfers(presenter);
    pv2_backend_fault_t fault;
    esp_err_t error = presenter->ops->get_fault(presenter->backend, &fault);
    if (error != ESP_OK || fault.result != ESP_OK) {
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, error != ESP_OK ? error : fault.result
        };
    }
    size_t index = PV2_MAX_FRAMES;
    pv2_lock_enter(&presenter->control, false);
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
        if (!presenter->transfers[i].cookie) {
            index = i;
            presenter->transfers[i] = (transfer_watch_t) {
                .cookie = cookie, .started_us = pv2_now_us()
            };
            break;
        }
    }
    pv2_lock_exit(&presenter->control, false);
    if (index == PV2_MAX_FRAMES) {
        return (pv2_tx_result_t) {
            PV2_TX_REJECTED, ESP_ERR_NO_MEM
        };
    }
    pv2_tx_result_t result = presenter->panel.submit(presenter->panel.context, surface, area, cookie);
    if (result.acceptance == PV2_TX_REJECTED) {
        pv2_lock_enter(&presenter->control, false);
        /* An early completion may already have consumed this timing record. */
        if (presenter->transfers[index].cookie == cookie) {
            presenter->transfers[index].cookie = 0;
        }
        pv2_lock_exit(&presenter->control, false);
    }
    expire_transfers(presenter);
    (void)pv2_event_signal(presenter->work, false);
    return result;
}

static esp_err_t panel_quiesce(void *context, uint32_t timeout)
{
    pv2_presenter_t *presenter = context;
    /* Underlying quiesce joins the entire panel_event callback, including notify. */
    expire_transfers(presenter);
    esp_err_t error = presenter->panel.quiesce(presenter->panel.context, timeout);
    expire_transfers(presenter);
    if (error == ESP_OK && presenter->transfers) {
        pv2_lock_enter(&presenter->control, false);
        for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
            presenter->transfers[i].cookie = 0;
        }
        pv2_lock_exit(&presenter->control, false);
    }
    return error;
}

static bool exiting(pv2_presenter_t *presenter)
{
    pv2_lock_enter(&presenter->control, false);
    bool exit = presenter->exit_requested;
    pv2_lock_exit(&presenter->control, false);
    return exit;
}

static void request_exit(pv2_presenter_t *presenter)
{
    pv2_lock_enter(&presenter->control, false);
    presenter->exit_requested = true;
    pv2_lock_exit(&presenter->control, false);
    (void)pv2_event_signal(presenter->work, false);
}

static esp_err_t pump_backend(pv2_presenter_t *presenter)
{
    expire_transfers(presenter);
    return presenter->kind == PV2_PRESENTER_TE ? pv2_te_service(presenter->backend) :
           presenter->kind == PV2_PRESENTER_FB ? pv2_fb_service(presenter->backend) : pv2_gram_service(presenter->backend);
}

static void sender(void *context)
{
    pv2_presenter_t *presenter = context;
    for (;;) {
        (void)pv2_event_wait(presenter->work, transfer_wait_ms(presenter));
        if (exiting(presenter)) {
            return;
        }
        (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
        if (exiting(presenter)) {
            pv2_mutex_give(presenter->gate);
            return;
        }
        presenter->last_service_error = pump_backend(presenter);
        pv2_mutex_give(presenter->gate);
        /* Wake owner only; signaling work here would cause a busy loop. */
        (void)pv2_event_signal(presenter->changed, false);
    }
}

static void free_presenter(pv2_presenter_t *presenter)
{
    pv2_worker_destroy(presenter->worker);
    pv2_event_destroy(presenter->work);
    pv2_event_destroy(presenter->changed);
    pv2_mutex_destroy(presenter->gate);
    pv2_lock_destroy(&presenter->control);
    pv2_free(presenter->transfers);
    pv2_free(presenter);
}

static esp_err_t check_owner(pv2_presenter_t *presenter)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    return pv2_same_task(presenter->owner, pv2_current_task()) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static uint32_t remaining(uint64_t start, uint32_t timeout)
{
    if (timeout == UINT32_MAX) {
        return timeout;
    }
    uint64_t elapsed = pv2_now_ms() - start;
    return elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
}

static void record_error(pv2_presenter_t *presenter, esp_err_t error)
{
    if (presenter->first_error == ESP_OK) {
        presenter->first_error = error;
    }
}

static esp_err_t refresh_fault(pv2_presenter_t *presenter)
{
    expire_transfers(presenter);
    pv2_backend_fault_t fault;
    esp_err_t ret = presenter->ops->get_fault(presenter->backend, &fault);
    if (ret == ESP_OK) {
        record_error(presenter, fault.result);
    }
    return ret;
}

static esp_err_t reap_ready(pv2_presenter_t *presenter)
{
    while (presenter->count) {
        pv2_frame_status_t status;
        esp_err_t ret = presenter->ops->query(presenter->backend, presenter->receipts[presenter->head], &status);
        if (ret != ESP_OK) {
            return ret;
        }
        if (!status.terminal) {
            break;
        }
        ret = presenter->ops->release_receipt(presenter->backend, presenter->receipts[presenter->head]);
        if (ret != ESP_OK) {
            return ret;
        }
        for (size_t i = 0; i < PV2_MAX_FRAMES; ++i) {
            observer_t *observer = &presenter->observers[i];
            if (observer->token && observer->backend.value == presenter->receipts[presenter->head].value) {
                observer->status = status;
            }
        }
        record_error(presenter, status.result);
        presenter->head = (presenter->head + 1) % presenter->limit;
        --presenter->count;
        ++presenter->completed;
    }
    return refresh_fault(presenter);
}

/* Called with gate held; only owner waits here. Worker has a separate event,
 * so it cannot consume the owner's notification. Always recheck after wake. */
static esp_err_t wait_change(pv2_presenter_t *presenter, uint64_t start, uint32_t timeout)
{
    uint32_t budget = remaining(start, timeout);
    if (!budget) {
        return ESP_ERR_TIMEOUT;
    }
    uint32_t transfer_budget = transfer_wait_ms(presenter);
    if (transfer_budget < budget) {
        budget = transfer_budget;
    }
    uint32_t slice = (presenter->kind == PV2_PRESENTER_TE && budget > 1) ? 1 : budget;
    pv2_mutex_give(presenter->gate);
    bool signaled = pv2_event_wait(presenter->changed, slice);
    /* Reacquire before returning even on timeout: the caller must not inspect
     * backend state concurrently. Latency of an already running driver call
     * is not a hard real-time bound; adapter submit must return promptly. */
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    expire_transfers(presenter);
    return (signaled || remaining(start, timeout) > 0) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t wait_receipt(pv2_presenter_t *presenter, pv2_receipt_t receipt, uint32_t timeout,
                              pv2_frame_status_t *status)
{
    uint64_t start = pv2_now_ms();
    for (;;) {
        esp_err_t ret = presenter->ops->query(presenter->backend, receipt, status);
        if (ret != ESP_OK || status->terminal) {
            return ret;
        }
        ret = wait_change(presenter, start, timeout);
        if (ret != ESP_OK) {
            return ret;
        }
    }
}

static bool valid_lease(pv2_presenter_t *presenter, const pv2_lease_t *lease)
{
    return lease && presenter->held.id.value && lease->id.value == presenter->held.id.value &&
           lease->surface.pixels == presenter->held.surface.pixels &&
           lease->surface.capacity_bytes == presenter->held.surface.capacity_bytes;
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
    esp_err_t error = pv2_config_resolve_legacy(&input, &resolved);
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

esp_err_t pv2_presenter_rebind_producer(pv2_presenter_t *presenter, uint32_t timeout)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (presenter->state == PV2_PRESENTER_RUNNING && !presenter->frame.value && !presenter->held.id.value && !presenter->canceling) {
        presenter->owner = pv2_current_task();
        error = ESP_OK;
    }
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_get_draw_buffers(pv2_presenter_t *presenter, void **buffers,
                                         size_t capacity, size_t *count, size_t *bytes)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    error = presenter->ops->get_draw_buffers(presenter->backend, buffers, capacity, count, bytes);
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_create(const pv2_presenter_config_t *config, pv2_presenter_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    pv2_presenter_config_t resolved;
    esp_err_t resolution = pv2_config_resolve_legacy(config, &resolved);
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
    pv2_presenter_t *presenter = pv2_alloc(sizeof(*presenter));
    if (!presenter) {
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&presenter->state, PV2_PRESENTER_RUNNING);
    if (!pv2_lock_init(&presenter->control)) {
        pv2_free(presenter);
        return ESP_ERR_NO_MEM;
    }
    presenter->gate = pv2_mutex_create();
    presenter->changed = pv2_event_create();
    presenter->work = pv2_event_create();
    if (!presenter->gate || !presenter->changed || !presenter->work) {
        free_presenter(presenter);
        return ESP_ERR_NO_MEM;
    }
    presenter->kind = config->backend_kind;
    presenter->output = config->output_caps;
    if (!presenter->output.x_alignment) {
        presenter->output.x_alignment = 1;
    }
    if (!presenter->output.y_alignment) {
        presenter->output.y_alignment = 1;
    }
    if (!presenter->output.width_alignment) {
        presenter->output.width_alignment = 1;
    }
    if (!presenter->output.height_alignment) {
        presenter->output.height_alignment = 1;
    }
    presenter->rotation = config->backend.rotation;
    presenter->ops = presenter->kind == PV2_PRESENTER_TE ? &pv2_te_backend :
                     presenter->kind == PV2_PRESENTER_FB ? &pv2_fb_backend : &pv2_gram_backend;
    presenter->event_isr = presenter->ops->on_event_isr;
    presenter->owner = pv2_current_task();
    presenter->timeout = config->timeout_ms;
    presenter->transfer_timeout_us = (uint64_t)config->transfer_timeout_ms * 1000;
    if (presenter->transfer_timeout_us) {
        presenter->transfers = pv2_alloc(PV2_MAX_FRAMES * sizeof(*presenter->transfers));
        if (!presenter->transfers) {
            free_presenter(presenter);
            return ESP_ERR_NO_MEM;
        }
    }
    presenter->limit = config->backend.max_frames;
    presenter->panel = config->backend.panel;
    presenter->user_progress = config->backend.on_progress;
    presenter->progress_user = config->backend.progress_user;
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    if (config->background_service) {
        presenter->worker = pv2_worker_create(sender, presenter, config->worker_stack_bytes ? config->worker_stack_bytes : 4096,
                                              config->worker_priority ? config->worker_priority : 5);
        if (!presenter->worker) {
            pv2_mutex_give(presenter->gate);
            free_presenter(presenter);
            return ESP_ERR_NO_MEM;
        }
    }
    pv2_backend_config_t backend = config->backend;
    backend.panel = (pv2_panel_adapter_t) {
        presenter, panel_attach, panel_submit, panel_quiesce
    };
    backend.on_progress = copy_progress;
    backend.progress_user = presenter;
    esp_err_t ret = presenter->ops->create(&backend, &presenter->backend);
    if (ret != ESP_OK) {
        request_exit(presenter);
        pv2_mutex_give(presenter->gate);
        if (presenter->worker) {
            (void)pv2_worker_join(presenter->worker, UINT32_MAX);
        }
        free_presenter(presenter);
        return ret;
    }
    (void)presenter->ops->get_caps(presenter->backend, &presenter->caps);
    pv2_mutex_give(presenter->gate);
    *out = presenter;
    return ESP_OK;
}

static esp_err_t service_impl(pv2_presenter_t *presenter)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_err_t service = pump_backend(presenter);
    presenter->last_service_error = service;
    ret = reap_ready(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->first_error != ESP_OK) {
        return presenter->first_error;
    }
    return service;
}

void pv2_presenter_notify_te(pv2_presenter_t *presenter, uint64_t timestamp_us)
{
    if (!presenter || presenter->kind != PV2_PRESENTER_TE) {
        return;
    }
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp_us};
    pv2_te_backend.on_event(presenter->backend, &event);
    (void)notify(presenter, false);
}

bool PV2_ISR_ATTR pv2_presenter_notify_te_isr(pv2_presenter_t *presenter, uint64_t timestamp_us)
{
    if (!presenter || presenter->kind != PV2_PRESENTER_TE) {
        return false;
    }
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp_us};
    bool yield = presenter->event_isr(presenter->backend, &event);
    yield |= notify(presenter, true);
    return yield;
}

static esp_err_t get_caps_impl(pv2_presenter_t *presenter, pv2_backend_caps_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = presenter->caps;
    return ESP_OK;
}

static esp_err_t begin_next_frame_impl(pv2_presenter_t *presenter, const pv2_frame_request_t *request, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    if (presenter->state != PV2_PRESENTER_RUNNING || presenter->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = reap_ready(presenter);
        if (ret != ESP_OK) {
            return ret;
        }
        if (presenter->first_error != ESP_OK) {
            return presenter->first_error;
        }
        if (presenter->count < presenter->limit) {
            ret = presenter->ops->begin(presenter->backend, request, &presenter->frame);
            if (ret != ESP_ERR_NO_MEM || !presenter->count) {
                return ret;
            }
        }
        /* Wake on any copy drain or VSYNC completion, not only head terminal:
         * post-seal async copy drain frees b->active in microseconds without
         * waiting for a full VSYNC period. */
        ret = wait_change(presenter, start, timeout);
        if (ret != ESP_OK) {
            return ret;
        }
    }
}

static esp_err_t acquire_buffer_impl(pv2_presenter_t *presenter, pv2_lease_t *out, uint32_t timeout)
{
    if (out) {
        *out = (pv2_lease_t) {
            0
        };
    }
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (presenter->state != PV2_PRESENTER_RUNNING || !presenter->frame.value || presenter->held.id.value || presenter->canceling) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = refresh_fault(presenter);
        if (ret != ESP_OK || presenter->first_error != ESP_OK) {
            return ret != ESP_OK ? ret : presenter->first_error;
        }
        ret = presenter->ops->acquire(presenter->backend, presenter->frame, NULL, 0, &presenter->held);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(presenter, start, timeout);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ret == ESP_OK) {
        *out = presenter->held;
    }
    return ret;
}

static esp_err_t resolve_rows_impl(pv2_presenter_t *presenter, const pv2_lease_t *lease,
                                   size_t stride, uint32_t requested, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out || !stride || !requested) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!valid_lease(presenter, lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bpp = presenter->caps.draw_layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (stride % bpp || stride > presenter->held.surface.capacity_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t rows = presenter->held.surface.capacity_bytes / stride;
    *out = rows < requested ? (uint32_t)rows : requested;
    if (presenter->kind == PV2_PRESENTER_GRAM && *out < requested) {
        bool quarter = presenter->rotation == PV2_ROTATE_90 || presenter->rotation == PV2_ROTATE_270;
        uint32_t a = quarter ? presenter->output.x_alignment : presenter->output.y_alignment;
        uint32_t extent = quarter ? presenter->output.width_alignment : presenter->output.height_alignment;
        uint32_t u = a, v = extent;
        while (v) {
            uint32_t t = u % v;
            u = v;
            v = t;
        }
        uint64_t step = (uint64_t)a / u * extent;
        if (step > *out) {
            return ESP_ERR_INVALID_SIZE;
        }
        *out -= *out % (uint32_t)step;
    }
    return ESP_OK;
}

static esp_err_t submit_buffer_impl(pv2_presenter_t *presenter, const pv2_lease_t *lease,
                                    const pv2_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!area || !stride || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1 ||
            (uint32_t)area->x2 >= presenter->caps.draw_layout.width || (uint32_t)area->y2 >= presenter->caps.draw_layout.height) {
        return ESP_ERR_INVALID_ARG;
    }
    if (presenter->state != PV2_PRESENTER_RUNNING || presenter->canceling || !valid_lease(presenter, lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_region_submit_t submit = { .logical_area = { (uint32_t)area->x1, (uint32_t)area->y1,
                                                     (uint32_t)(area->x2 - area->x1) + 1, (uint32_t)(area->y2 - area->y1) + 1
                                                   }, .stride_bytes = stride
                                 };
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = refresh_fault(presenter);
        if (ret != ESP_OK || presenter->first_error != ESP_OK) {
            return ret != ESP_OK ? ret : presenter->first_error;
        }
        ret = presenter->ops->submit_region_ex(presenter->backend, presenter->frame, presenter->held.id, &submit);
        if (ret != ESP_ERR_NO_MEM && ret != ESP_ERR_TIMEOUT) {
            break;
        }
        if (wait_change(presenter, start, presenter->timeout) != ESP_OK) {
            break;
        }
    }
    if (ret == ESP_OK) {
        presenter->held = (pv2_lease_t) {
            0
        };
    }
    return ret;
}

static esp_err_t commit_frame_impl(pv2_presenter_t *presenter, const pv2_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->state != PV2_PRESENTER_RUNNING || !presenter->frame.value || presenter->canceling || presenter->held.id.value || presenter->count == presenter->limit) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_receipt_t receipt;
    ret = presenter->ops->end_ex(presenter->backend, presenter->frame, coverage, &receipt);
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->receipts[(presenter->head + presenter->count) % presenter->limit] = receipt;
    ++presenter->count;
    ++presenter->committed;
    presenter->frame.value = 0;
    (void)pv2_event_signal(presenter->work, false);
    return ESP_OK;
}

/* Gate held. Public cancellation requires the owner. The private facade
 * bridge requires its producer session to have already revoked the frame. */
static esp_err_t cancel_frame_impl(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret;
    if (!presenter->frame.value) {
        return ESP_OK;
    }
    presenter->canceling = true;
    uint64_t start = pv2_now_ms();
    for (;;) {
        ret = presenter->ops->cancel_frame(presenter->backend, presenter->frame, 0);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(presenter, start, timeout);
        if (ret != ESP_OK) {
            break;
        }
    }
    presenter->held = (pv2_lease_t) {
        0
    };
    if (ret == ESP_OK) {
        presenter->frame.value = 0;
        presenter->canceling = false;
    }
    return ret;
}

static esp_err_t drain_impl(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = pv2_now_ms();
    while (presenter->count) {
        pv2_frame_status_t status;
        ret = wait_receipt(presenter, presenter->receipts[presenter->head], remaining(start, timeout), &status);
        if (ret != ESP_OK) {
            return ret;
        }
        ret = reap_ready(presenter);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    ret = refresh_fault(presenter);
    return ret != ESP_OK ? ret : presenter->first_error;
}

static esp_err_t get_status_impl(pv2_presenter_t *presenter, pv2_presenter_status_t *out)
{
    esp_err_t ret;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = reap_ready(presenter);
    *out = (pv2_presenter_status_t) {
        .state = presenter->state, .frame_active = presenter->frame.value != 0,
        .cancel_pending = presenter->canceling, .pending_frames = presenter->count,
        .committed_frames = presenter->committed, .completed_frames = presenter->completed, .first_error = presenter->first_error, .last_service_error = presenter->last_service_error
    };
    return ret;
}

static esp_err_t stop_impl(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret;
    presenter->state = PV2_PRESENTER_CLOSING;
    ret = presenter->ops->stop(presenter->backend, timeout);
    presenter->held = (pv2_lease_t) {
        0
    };
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->frame.value = 0;
    presenter->canceling = false;
    ret = reap_ready(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->state = PV2_PRESENTER_STOPPED;
    return ESP_OK;
}

static esp_err_t destroy_impl(pv2_presenter_t *presenter)
{
    esp_err_t ret;
    if (presenter->state != PV2_PRESENTER_STOPPED || presenter->count || presenter->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i)
        if (presenter->observers[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    ret = presenter->ops->destroy(presenter->backend);
    return ret;
}

esp_err_t pv2_presenter_service(pv2_presenter_t *presenter)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = service_impl(presenter);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_get_caps(pv2_presenter_t *presenter, pv2_backend_caps_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = get_caps_impl(presenter, out);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_begin_budget(pv2_presenter_t *presenter, const pv2_frame_request_t *request, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (presenter->state != PV2_PRESENTER_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = begin_next_frame_impl(presenter, request, remaining(start, timeout));
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_begin_next_frame(pv2_presenter_t *presenter, const pv2_frame_request_t *request)
{
    return presenter ? pv2_presenter_begin_budget(presenter, request, presenter->timeout) : ESP_ERR_INVALID_ARG;
}

esp_err_t pv2_presenter_acquire_buffer(pv2_presenter_t *presenter, pv2_lease_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (presenter->state != PV2_PRESENTER_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pv2_mutex_take(presenter->gate, presenter->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = acquire_buffer_impl(presenter, out, remaining(start, presenter->timeout));
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_resolve_rows(pv2_presenter_t *presenter, const pv2_lease_t *lease, size_t stride, uint32_t requested, uint32_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = resolve_rows_impl(presenter, lease, stride, requested, out);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_submit_buffer(pv2_presenter_t *presenter, const pv2_lease_t *lease, const pv2_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = submit_buffer_impl(presenter, lease, area, stride);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_commit_frame(pv2_presenter_t *presenter, const pv2_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = commit_frame_impl(presenter, coverage);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_cancel_budget(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = cancel_frame_impl(presenter, remaining(start, timeout));
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_cancel_frame(pv2_presenter_t *presenter)
{
    return presenter ? pv2_presenter_cancel_budget(presenter, presenter->timeout) : ESP_ERR_INVALID_ARG;
}

esp_err_t pv2_presenter_control_finish_cancel(pv2_presenter_t *presenter, uint32_t timeout)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = cancel_frame_impl(presenter, remaining(start, timeout));
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_drain(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    ret = drain_impl(presenter, remaining(start, timeout));
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_control_get_status(pv2_presenter_t *presenter, pv2_presenter_status_t *out)
{
    esp_err_t ret = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) {
        return ret;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = get_status_impl(presenter, out);
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_get_status(pv2_presenter_t *presenter, pv2_presenter_status_t *out)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? pv2_presenter_control_get_status(presenter, out) : error;
}

esp_err_t pv2_presenter_control_stop(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t ret = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = pv2_now_ms();
    /* Publish closing before joining sender. Private control callers have
     * stopped the producer; task-neutral observation reads this atomically. */
    presenter->state = PV2_PRESENTER_CLOSING;
    request_exit(presenter);
    if (presenter->worker && !pv2_worker_join(presenter->worker, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!pv2_mutex_take(presenter->gate, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    ret = stop_impl(presenter, remaining(start, timeout));
    pv2_mutex_give(presenter->gate);
    return ret;
}

esp_err_t pv2_presenter_stop(pv2_presenter_t *presenter, uint32_t timeout)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? pv2_presenter_control_stop(presenter, timeout) : error;
}

esp_err_t pv2_presenter_control_destroy(pv2_presenter_t *presenter)
{
    esp_err_t ret = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->state != PV2_PRESENTER_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    ret = destroy_impl(presenter);
    pv2_mutex_give(presenter->gate);
    if (ret == ESP_OK) {
        free_presenter(presenter);
    }
    return ret;
}

esp_err_t pv2_presenter_destroy(pv2_presenter_t *presenter)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? pv2_presenter_control_destroy(presenter) : error;
}

static observer_t *find_observer(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt)
{
    for (size_t i = 0; receipt.value && i < PV2_MAX_FRAMES; ++i)
        if (presenter->observers[i].token == receipt.value) {
            return &presenter->observers[i];
        }
    return NULL;
}

esp_err_t pv2_presenter_commit_with_receipt(pv2_presenter_t *presenter, const pv2_frame_coverage_t *coverage,
                                            pv2_presenter_receipt_t *receipt)
{
    if (!receipt) {
        return ESP_ERR_INVALID_ARG;
    }
    receipt->value = 0;
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    observer_t *observer = NULL;
    for (size_t i = 0; i < PV2_MAX_FRAMES; ++i)
        if (!presenter->observers[i].token) {
            observer = &presenter->observers[i];
            break;
        }
    uint64_t token = observer ? pv2_new_token() : 0;
    if (!token) {
        error = ESP_ERR_NO_MEM;
    } else {
        error = commit_frame_impl(presenter, coverage);
        if (error == ESP_OK) {
            *observer = (observer_t) {
                .token = token, .backend = presenter->receipts[(presenter->head + presenter->count - 1) % presenter->limit],
                .status = {.sealed = true}
            };
            receipt->value = token;
        }
    }
    pv2_mutex_give(presenter->gate);
    return error;
}

static esp_err_t query_observer(pv2_presenter_t *presenter, observer_t *observer, pv2_frame_status_t *out)
{
    expire_transfers(presenter);
    if (!observer) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!observer->status.terminal) {
        esp_err_t error = presenter->ops->query(presenter->backend, observer->backend, &observer->status);
        if (error != ESP_OK) {
            return error;
        }
    }
    *out = observer->status;
    return ESP_OK;
}

esp_err_t pv2_presenter_control_query_frame(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt, pv2_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    error = query_observer(presenter, find_observer(presenter, receipt), out);
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_query_frame(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt, pv2_frame_status_t *out)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? pv2_presenter_control_query_frame(presenter, receipt, out) : error;
}

esp_err_t pv2_presenter_wait_frame(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt,
                                   pv2_completion_point_t point, uint32_t timeout, pv2_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (point != PV2_COMPLETION_SOURCE_RELEASED && point != PV2_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == PV2_COMPLETION_PRESENT_CONFIRMED && !presenter->caps.supports_present_confirmation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    observer_t *observer = find_observer(presenter, receipt);
    do {
        error = query_observer(presenter, observer, out);
        if (error != ESP_OK || out->terminal) {
            break;
        }
        error = wait_change(presenter, start, timeout);
    } while (error == ESP_OK);
    pv2_mutex_give(presenter->gate);
    return error;
}

static esp_err_t untrack(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt, bool require_terminal)
{
    if (!receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    observer_t *observer = find_observer(presenter, receipt);
    pv2_frame_status_t status;
    if (!observer) {
        error = ESP_ERR_INVALID_STATE;
    } else if (require_terminal) {
        error = query_observer(presenter, observer, &status);
        if (error == ESP_OK && !status.terminal) {
            error = ESP_ERR_INVALID_STATE;
        }
    }
    if (error == ESP_OK) {
        observer->token = 0;
    }
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_release_receipt(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? untrack(presenter, receipt, true) : error;
}

esp_err_t pv2_presenter_cancel_tracking(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? untrack(presenter, receipt, false) : error;
}

esp_err_t pv2_presenter_set_rotation(pv2_presenter_t *presenter, pv2_rotation_t rotation, uint32_t timeout)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!presenter->ops->set_rotation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = pv2_now_ms();
    if (!pv2_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    if (presenter->state != PV2_PRESENTER_RUNNING || presenter->frame.value || presenter->held.id.value) {
        error = ESP_ERR_INVALID_STATE;
    } else {
        error = drain_impl(presenter, remaining(start, timeout));
    }
    if (error == ESP_OK) {
        error = presenter->ops->set_rotation(presenter->backend, rotation);
    }
    if (error == ESP_OK) {
        presenter->rotation = rotation;
        error = presenter->ops->get_caps(presenter->backend, &presenter->caps);
    }
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_map_point(pv2_presenter_t *presenter, uint32_t x, uint32_t y, uint32_t *lx, uint32_t *ly)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    error = pv2_unrotate_point(presenter->caps.draw_layout.width, presenter->caps.draw_layout.height, presenter->rotation, x, y, lx, ly);
    pv2_mutex_give(presenter->gate);
    return error;
}

esp_err_t pv2_presenter_resolve_render_area(pv2_presenter_t *presenter, const pv2_rect_t *dirty, pv2_rect_t *render)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    pv2_resolved_plan_t plan = {.logical_layout = presenter->caps.draw_layout, .output = presenter->output};
    plan.normalized.backend.rotation = presenter->rotation;
    plan.normalized.backend.render_contract = presenter->caps.contract;
    plan.images.count = presenter->kind == PV2_PRESENTER_GRAM ? 0 : 1;
    plan.physical_layout = plan.logical_layout;
    if (presenter->rotation == PV2_ROTATE_90 || presenter->rotation == PV2_ROTATE_270) {
        plan.physical_layout.width = plan.logical_layout.height;
        plan.physical_layout.height = plan.logical_layout.width;
    }
    pv2_rect_t physical;
    error = pv2_plan_render_area(&plan, dirty, render, &physical);
    pv2_mutex_give(presenter->gate);
    return error;
}

size_t pv2_presenter_memory(const pv2_presenter_config_t *config)
{
    return sizeof(pv2_presenter_t) + (config->transfer_timeout_ms ? PV2_MAX_FRAMES * sizeof(transfer_watch_t) : 0) + pv2_mutex_memory() + 2 * pv2_event_memory() +
           (config->background_service ? pv2_worker_memory() : 0);
}

esp_err_t pv2_presenter_get_rotation(pv2_presenter_t *presenter, pv2_rotation_t *out)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)pv2_mutex_take(presenter->gate, UINT32_MAX);
    *out = presenter->rotation;
    pv2_mutex_give(presenter->gate);
    return ESP_OK;
}

/* Control-plane observation owns no producer lease. Caller excludes deletion. */
esp_err_t pv2_presenter_control_release(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt)
{
    return untrack(presenter, receipt, true);
}

esp_err_t pv2_presenter_control_cancel_tracking(pv2_presenter_t *presenter, pv2_presenter_receipt_t receipt)
{
    return untrack(presenter, receipt, false);
}
