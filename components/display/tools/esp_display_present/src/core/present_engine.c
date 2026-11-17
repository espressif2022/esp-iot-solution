/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "backend/present_backend_config.h"
#include "core/present_producer.h"
#include "core/present_control.h"
#include "backend/present_backend.h"
#include "resources/present_memory.h"
#include "core/present_engine.h"
#include "config/present_plan.h"
#include "config/present_mode.h"
#include "platform/present_os.h"
#include "platform/present_limits.h"
#include "core/present_transfer_tracker.h"
#include "core/present_completion_tracker.h"
#include <limits.h>
#include <stdatomic.h>
#include <string.h>

enum { FRAME_DURATION_BUCKETS = 128, FRAME_DURATION_BUCKET_US = 1000 };

typedef struct {
    esp_display_presenter_metrics_t summary;
    uint64_t frame_buckets[FRAME_DURATION_BUCKETS];
    uint64_t frame_started_us, frame_max_us, pending_scanout_frames, scanout_cookie;
    bool frame_started, backlog_at_vsync, switched_since_vsync;
} present_metrics_t;

struct esp_display_present_engine {
    present_backend_t *backend;
    present_mutex_t *gate;
    present_os_event_t *changed;
    present_os_event_t *work;
    struct {
        present_worker_t *handle;
        present_task_t task;
        bool ready;
    } backend_worker;
    bool deferred_region;
    bool backend_only_submit;
    bool copy_on_backend_worker;
    present_lock_t control;
    present_metrics_t *metrics; /* Opt-in sampler protected by control. */
    present_transfer_tracker_t transfer;
    bool exit_requested;
    esp_display_present_panel_adapter_t panel;
    esp_display_present_progress_fn user_progress;
    void *progress_user;
    esp_display_present_panel_event_fn backend_event;
    void *backend_event_user;
    esp_err_t last_service_error;
    const present_backend_ops_t *ops;
    esp_display_present_engine_backend_t kind;
    esp_display_present_engine_rotation_t rotation;
    bool (*event_isr)(present_backend_t *, const esp_display_present_backend_event_t *);
    esp_display_present_backend_caps_t caps;
    esp_display_present_output_caps_t output;
    present_task_t owner;
    esp_display_present_frame_t frame;
    esp_display_present_lease_t held;
    present_completion_tracker_t completion;
    uint32_t timeout;
    bool canceling;
    _Atomic esp_display_present_engine_state_t state;
};

static bool PRESENT_ISR_ATTR notify(esp_display_present_engine_t *presenter, bool isr)
{
    bool yield = present_event_signal(presenter->changed, isr);
    yield |= present_event_signal(presenter->work, isr);
    return yield;
}

/* The control lock protects timing stamps; never call the backend under it.
 * A completion which races the task timeout reports FAULT before the genuine
 * DONE itself, so a successful receipt cannot escape ahead of the timeout. */
static bool PRESENT_ISR_ATTR finish_transfer(esp_display_present_engine_t *presenter, const esp_display_present_backend_event_t *event, bool isr)
{
    esp_display_present_event_kind_t completion = presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_FB ?
                                                  ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED : ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE;
    uint64_t now;
    if (event->kind != completion || !present_transfer_finish(&presenter->transfer, &presenter->control, event, isr, &now)) {
        return false;
    }
    const esp_display_present_backend_event_t failure = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_FAULT,
        .transaction_cookie = event->transaction_cookie,
        .timestamp_us = now,
        .result = ESP_ERR_TIMEOUT,
    };
    return presenter->backend_event(presenter->backend_event_user, &failure, isr);
}

/* Task-only, presenter gate held: submissions cannot start during the scan. */
static void expire_transfers(esp_display_present_engine_t *presenter)
{
    if (!presenter->transfer.timeout_us) {
        return;
    }
    uint64_t expired[PRESENT_MAX_FRAMES];
    uint64_t now = present_now_us();
    size_t count = present_transfer_expired(&presenter->transfer, &presenter->control, now, expired);
    for (size_t i = 0; i < count; ++i) {
        const esp_display_present_backend_event_t event = {
            .kind = ESP_DISPLAY_PRESENT_EVENT_FAULT,
            .transaction_cookie = expired[i],
            .timestamp_us = now,
            .result = ESP_ERR_TIMEOUT,
        };
        (void)presenter->backend_event(presenter->backend_event_user, &event, false);
    }
    if (count) {
        if (presenter->panel.request_abort) {
            presenter->panel.request_abort(presenter->panel.context);
        }
        (void)notify(presenter, false);
    }
}

static uint32_t transfer_wait_ms(esp_display_present_engine_t *presenter)
{
    return present_transfer_wait_ms(&presenter->transfer, &presenter->control);
}

static void pixel_work_ready(void *user)
{
    esp_display_present_engine_t *presenter = user;
    (void)present_event_signal(presenter->work, false);
}

static bool pixel_progress(void *user, bool isr)
{
    esp_display_present_engine_t *presenter = user;
    bool yield = present_event_signal(presenter->changed, isr);
    if (!(presenter->copy_on_backend_worker && !isr && presenter->backend_worker.ready &&
            present_same_task(presenter->backend_worker.task, present_current_task()))) {
        yield |= present_event_signal(presenter->work, isr);
    }
    if (presenter->user_progress) {
        yield |= presenter->user_progress(presenter->progress_user, isr);
    }
    return yield;
}

void PRESENT_ISR_ATTR present_presenter_metrics_vsync(esp_display_present_engine_t *presenter, bool isr)
{
    present_lock_enter(&presenter->control, isr);
    present_metrics_t *metrics = presenter->metrics;
    if (metrics && metrics->summary.scanout_available) {
        ++metrics->summary.scanout_vsyncs;
        if (metrics->backlog_at_vsync && !metrics->switched_since_vsync) {
            ++metrics->summary.scanout_supply_misses;
        }
        metrics->backlog_at_vsync = metrics->pending_scanout_frames != 0;
        metrics->switched_since_vsync = false;
    }
    present_lock_exit(&presenter->control, isr);
}

static bool PRESENT_ISR_ATTR panel_event(void *context, const esp_display_present_backend_event_t *event, bool isr)
{
    esp_display_present_engine_t *presenter = context;
    bool yield = finish_transfer(presenter, event, isr);
    yield |= presenter->backend_event(presenter->backend_event_user, event, isr);
    if (presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_FB &&
            event->kind == ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED && event->transaction_cookie && event->result == ESP_OK) {
        present_lock_enter(&presenter->control, isr);
        present_metrics_t *metrics = presenter->metrics;
        if (metrics && metrics->pending_scanout_frames && event->transaction_cookie == metrics->scanout_cookie) {
            metrics->scanout_cookie = 0;
            --metrics->pending_scanout_frames;
            metrics->switched_since_vsync = true;
        }
        present_lock_exit(&presenter->control, isr);
    }
    yield |= notify(presenter, isr);
    return yield;
}

static esp_err_t panel_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    esp_display_present_engine_t *presenter = context;
    presenter->backend_event = callback;
    presenter->backend_event_user = user;
    return presenter->panel.attach(presenter->panel.context, panel_event, presenter);
}

/* REGION/TE SUBMITTING slots stay pinned while the backend_worker lends the task gate.
 * Backend task state is resolved only after reacquiring it. Stop joins backend_worker
 * before quiesce/destruction, so the adapter and callback context remain live. */
static esp_display_present_tx_result_t execute_panel_submit(esp_display_present_engine_t *presenter,
                                                            const esp_display_present_surface_t *surface,
                                                            const esp_display_present_rect_t *area, uint64_t cookie)
{
    if (presenter->backend_only_submit) {
        present_mutex_give(presenter->gate);
    }
    present_metrics_t *scanout_metrics = presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_FB ? presenter->metrics : NULL;
    if (scanout_metrics) {
        present_lock_enter(&presenter->control, false);
        scanout_metrics->scanout_cookie = cookie;
        present_lock_exit(&presenter->control, false);
    }
    esp_display_present_tx_result_t result = presenter->panel.submit(presenter->panel.context, surface, area, cookie);
    if (scanout_metrics && result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED) {
        present_lock_enter(&presenter->control, false);
        scanout_metrics->scanout_cookie = 0;
        present_lock_exit(&presenter->control, false);
    }
    if (presenter->backend_only_submit) {
        (void)present_mutex_take(presenter->gate, UINT32_MAX);
    }
    return result;
}

/* Called under the engine gate, before TE pins a frame or consumes an edge. */
static bool panel_can_submit(void *context)
{
    esp_display_present_engine_t *presenter = context;
    bool allowed = presenter->state == ESP_DISPLAY_PRESENT_ENGINE_RUNNING &&
                   presenter->backend_worker.ready &&
                   present_same_task(presenter->backend_worker.task, present_current_task());
    if (!allowed && presenter->state == ESP_DISPLAY_PRESENT_ENGINE_RUNNING) {
        (void)present_event_signal(presenter->work, false);
    }
    return allowed;
}

static esp_display_present_tx_result_t panel_submit(void *context, const esp_display_present_surface_t *surface,
                                                    const esp_display_present_rect_t *area, uint64_t cookie)
{
    esp_display_present_engine_t *presenter = context;
    /* Foreground query/acquire may service READY slots. Only the backend_worker can
     * enter the driver; this also serializes submissions while its gate is lent. */
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_STATE
        };
    }
    if (presenter->backend_only_submit && (!presenter->backend_worker.ready ||
                                           !present_same_task(presenter->backend_worker.task, present_current_task()))) {
        (void)present_event_signal(presenter->work, false);
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM
        };
    }
    if (area->x % presenter->output.x_alignment || area->y % presenter->output.y_alignment ||
            area->width % presenter->output.w_align || area->height % presenter->output.h_align)
        return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_INVALID_ARG
    };
    if (!presenter->transfer.timeout_us) {
        return execute_panel_submit(presenter, surface, area, cookie);
    }
    expire_transfers(presenter);
    esp_display_present_backend_fault_t fault;
    esp_err_t error = presenter->ops->get_fault(presenter->backend, &fault);
    if (error != ESP_OK || fault.result != ESP_OK) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, error != ESP_OK ? error : fault.result
        };
    }
    size_t index = present_transfer_begin(&presenter->transfer, &presenter->control, cookie);
    if (index == PRESENT_MAX_FRAMES) {
        return (esp_display_present_tx_result_t) {
            ESP_DISPLAY_PRESENT_TX_REJECTED, ESP_ERR_NO_MEM
        };
    }
    esp_display_present_tx_result_t result = execute_panel_submit(presenter, surface, area, cookie);
    if (result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED) {
        present_transfer_rejected(&presenter->transfer, &presenter->control, index, cookie);
    }
    expire_transfers(presenter);
    (void)present_event_signal(presenter->work, false);
    return result;
}

static esp_err_t panel_quiesce(void *context, uint32_t timeout)
{
    esp_display_present_engine_t *presenter = context;
    /* Underlying quiesce joins the entire panel_event callback, including notify. */
    expire_transfers(presenter);
    esp_err_t error = presenter->panel.quiesce(presenter->panel.context, timeout);
    expire_transfers(presenter);
    if (error == ESP_OK) {
        present_transfer_quiesced(&presenter->transfer, &presenter->control);
    }
    return error;
}

static bool exiting(esp_display_present_engine_t *presenter)
{
    present_lock_enter(&presenter->control, false);
    bool exit = presenter->exit_requested;
    present_lock_exit(&presenter->control, false);
    return exit;
}

static void request_exit(esp_display_present_engine_t *presenter)
{
    present_lock_enter(&presenter->control, false);
    presenter->exit_requested = true;
    present_lock_exit(&presenter->control, false);
    (void)present_event_signal(presenter->work, false);
}

static esp_err_t pump_backend(esp_display_present_engine_t *presenter)
{
    expire_transfers(presenter);
    return presenter->ops->service(presenter->backend);
}

/* Advances backend state and transfer deadlines; not limited to sending. */
static void backend_worker(void *context)
{
    esp_display_present_engine_t *presenter = context;
    /* Creation holds the gate until backend publication (or exit on failure).
     * The blocking copy phase below cannot rely on the per-iteration gate. */
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    presenter->backend_worker.task = present_current_task();
    presenter->backend_worker.ready = true;
    present_mutex_give(presenter->gate);
    for (;;) {
        (void)present_event_wait(presenter->work, transfer_wait_ms(presenter));
        for (;;) {
            if (exiting(presenter)) {
                return;
            }
            /* A hardware callback may be delayed indefinitely. Keep the owner
             * gate available so cancellation and bounded stop can retain ownership
             * and return TIMEOUT while this worker is still using the buffers. */
            while (presenter->ops->copy_work && presenter->ops->copy_work(presenter->backend)) {
                if (exiting(presenter)) {
                    return;
                }
            }
            (void)present_mutex_take(presenter->gate, UINT32_MAX);
            if (exiting(presenter)) {
                present_mutex_give(presenter->gate);
                return;
            }
            presenter->last_service_error = pump_backend(presenter);
            present_mutex_give(presenter->gate);
            if (!presenter->ops->copy_work || exiting(presenter) ||
                    !presenter->ops->copy_work(presenter->backend)) {
                break;
            }
        }
        if (exiting(presenter)) {
            return;
        }
        /* Wake owner only; signaling work here would cause a busy loop. */
        (void)present_event_signal(presenter->changed, false);
    }
}

static void free_presenter(esp_display_present_engine_t *presenter)
{
    present_worker_destroy(presenter->backend_worker.handle);
    present_event_destroy(presenter->work);
    present_event_destroy(presenter->changed);
    present_mutex_destroy(presenter->gate);
    present_lock_destroy(&presenter->control);
    present_transfer_deinit(&presenter->transfer);
    present_free(presenter->metrics);
    present_free(presenter);
}

static esp_err_t check_owner(esp_display_present_engine_t *presenter)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    return present_same_task(presenter->owner, present_current_task()) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static uint32_t remaining(uint64_t start, uint32_t timeout)
{
    if (timeout == UINT32_MAX) {
        return timeout;
    }
    uint64_t elapsed = present_now_ms() - start;
    return elapsed >= timeout ? 0 : timeout - (uint32_t)elapsed;
}

static esp_err_t refresh_fault(esp_display_present_engine_t *presenter)
{
    expire_transfers(presenter);
    esp_display_present_backend_fault_t fault;
    esp_err_t ret = presenter->ops->get_fault(presenter->backend, &fault);
    if (ret == ESP_OK) {
        present_completion_record_error(&presenter->completion, fault.result);
    }
    return ret;
}

static esp_err_t reap_ready(esp_display_present_engine_t *presenter, bool service)
{
    esp_err_t ret = present_completion_reap(&presenter->completion, presenter->ops, presenter->backend, service);
    return ret != ESP_OK ? ret : refresh_fault(presenter);
}

/* A running worker owns progress during bounded observer waits. Once close
 * begins, task-side service may retire completions but panel_submit rejects
 * any new transfer. Cooperative engines still need a foreground pump. */
static bool service_during_wait(const esp_display_present_engine_t *presenter)
{
    return !presenter->backend_worker.handle || presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING;
}

/* Called with gate held by the owner or exclusive session controller. The
 * controller excludes producer calls. Worker has a separate event, so it
 * cannot consume this waiter's notification. A deadline can expire before
 * reacquisition; the caller must then return without touching gated state. */
static esp_err_t wait_change(esp_display_present_engine_t *presenter, uint64_t start, uint32_t timeout, bool *gate_held)
{
    uint32_t budget = remaining(start, timeout);
    if (!budget) {
        return ESP_ERR_TIMEOUT;
    }
    uint32_t transfer_budget = transfer_wait_ms(presenter);
    if (transfer_budget < budget) {
        budget = transfer_budget;
    }
    uint32_t slice = (presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_TE && budget > 1) ? 1 : budget;
    present_mutex_give(presenter->gate);
    (void)present_event_wait(presenter->changed, slice);
    *gate_held = present_mutex_take(presenter->gate, remaining(start, timeout));
    if (!*gate_held) {
        return ESP_ERR_TIMEOUT;
    }
    expire_transfers(presenter);
    return remaining(start, timeout) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool valid_lease(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease)
{
    return lease && presenter->held.id.value && lease->id.value == presenter->held.id.value &&
           lease->surface.pixels == presenter->held.surface.pixels &&
           lease->surface.capacity_bytes == presenter->held.surface.capacity_bytes;
}

esp_err_t esp_display_present_engine_rebind_producer(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (presenter->state == ESP_DISPLAY_PRESENT_ENGINE_RUNNING && !presenter->frame.value && !presenter->held.id.value && !presenter->canceling) {
        error = refresh_fault(presenter);
        if (error == ESP_OK) {
            error = presenter->completion.first_error;
        }
        if (error == ESP_OK) {
            presenter->owner = present_current_task();
        }
    }
    present_mutex_give(presenter->gate);
    return error;
}

esp_err_t esp_display_present_engine_get_draw_buffers(esp_display_present_engine_t *presenter, void **buffers,
                                                      size_t capacity, size_t *count, size_t *bytes)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    error = presenter->ops->get_draw_buffers(presenter->backend, buffers, capacity, count, bytes);
    present_mutex_give(presenter->gate);
    return error;
}

esp_err_t esp_display_present_engine_create(const esp_display_present_engine_config_t *config, esp_display_present_engine_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    esp_display_present_engine_config_t resolved;
    esp_err_t resolution = present_mode_resolve_engine(config, ESP_DISPLAY_PRESENT_PLAN_CURRENT_DEFAULTS, false, &resolved);
    if (resolution != ESP_OK) {
        return resolution;
    }
    config = &resolved;
    if (config->backend.copy_on_backend_worker &&
            (config->backend_kind == ESP_DISPLAY_PRESENT_ENGINE_REGION || !config->backend.async_copy || !config->background_service ||
             (config->backend_kind == ESP_DISPLAY_PRESENT_ENGINE_TE && config->backend.compose_count != 1))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->backend.width > INT_MAX || config->backend.height > INT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!config->backend.panel.attach || !config->backend.panel.submit || !config->backend.panel.quiesce) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_display_present_engine_t *presenter = present_alloc(sizeof(*presenter));
    if (!presenter) {
        return ESP_ERR_NO_MEM;
    }
    atomic_init(&presenter->state, ESP_DISPLAY_PRESENT_ENGINE_RUNNING);
    if (!present_lock_init(&presenter->control)) {
        present_free(presenter);
        return ESP_ERR_NO_MEM;
    }
    presenter->gate = present_mutex_create();
    presenter->changed = present_event_create();
    presenter->work = present_event_create();
    if (!presenter->gate || !presenter->changed || !presenter->work) {
        free_presenter(presenter);
        return ESP_ERR_NO_MEM;
    }
    presenter->kind = config->backend_kind;
    switch (esp_display_present_resolve_exec_mode(config)) {
    case ESP_DISPLAY_PRESENT_EXEC_REGION_DEFERRED:
        presenter->deferred_region = true;
        presenter->backend_only_submit = true;
        presenter->copy_on_backend_worker = false;
        break;
    case ESP_DISPLAY_PRESENT_EXEC_TE_SHARED:
        presenter->deferred_region = false;
        presenter->backend_only_submit = true;
        presenter->copy_on_backend_worker = true;
        break;
    case ESP_DISPLAY_PRESENT_EXEC_TE_SPLIT:
        presenter->deferred_region = false;
        presenter->backend_only_submit = true;
        presenter->copy_on_backend_worker = false;
        break;
    case ESP_DISPLAY_PRESENT_EXEC_FB_SHARED:
        presenter->deferred_region = false;
        presenter->backend_only_submit = false;
        presenter->copy_on_backend_worker = true;
        break;
    default:
        presenter->deferred_region = false;
        presenter->backend_only_submit = false;
        presenter->copy_on_backend_worker = false;
        break;
    }
    presenter->output = config->output_caps;
    if (!presenter->output.x_alignment) {
        presenter->output.x_alignment = 1;
    }
    if (!presenter->output.y_alignment) {
        presenter->output.y_alignment = 1;
    }
    if (!presenter->output.w_align) {
        presenter->output.w_align = 1;
    }
    if (!presenter->output.h_align) {
        presenter->output.h_align = 1;
    }
    presenter->rotation = config->backend.rotation;
    presenter->ops = presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_TE ? &present_te_backend :
                     presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_FB ? &present_fb_backend : &present_region_backend;
    presenter->event_isr = presenter->ops->on_event_isr;
    presenter->owner = present_current_task();
    presenter->timeout = config->timeout_ms;
    if (present_transfer_init(&presenter->transfer, config->transfer_timeout_ms) != ESP_OK) {
        free_presenter(presenter);
        return ESP_ERR_NO_MEM;
    }
    presenter->completion.limit = config->backend.max_frames;
    presenter->panel = config->backend.panel;
    presenter->user_progress = config->backend.on_progress;
    presenter->progress_user = config->backend.progress_user;
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    if (config->background_service) {
        presenter->backend_worker.handle = present_worker_create("present_backend", backend_worker, presenter, config->worker_stack_bytes ? config->worker_stack_bytes : 4096,
                                                                 config->worker_priority ? config->worker_priority : 5);
        if (!presenter->backend_worker.handle) {
            present_mutex_give(presenter->gate);
            free_presenter(presenter);
            return ESP_ERR_NO_MEM;
        }
    }
    esp_display_present_backend_config_t backend = config->backend;
    backend.deferred_submit = presenter->deferred_region;
    if (presenter->backend_only_submit && presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_TE) {
        backend.can_submit = panel_can_submit;
        backend.submit_user = presenter;
    }
    backend.panel = (esp_display_present_panel_adapter_t) {
        presenter, panel_attach, panel_submit, panel_quiesce, NULL
    };
    backend.on_progress = pixel_progress;
    backend.progress_user = presenter;
    backend.on_work = pixel_work_ready;
    backend.work_user = presenter;
    esp_err_t ret = presenter->ops->create_scheduled ?
                    presenter->ops->create_scheduled(&backend, &presenter->backend, config->background_service) :
                    presenter->ops->create(&backend, &presenter->backend);
    if (ret != ESP_OK) {
        request_exit(presenter);
        present_mutex_give(presenter->gate);
        if (presenter->backend_worker.handle) {
            (void)present_worker_join(presenter->backend_worker.handle, UINT32_MAX);
        }
        free_presenter(presenter);
        return ret;
    }
    (void)presenter->ops->get_caps(presenter->backend, &presenter->caps);
    present_mutex_give(presenter->gate);
    *out = presenter;
    return ESP_OK;
}

static esp_err_t service_impl(esp_display_present_engine_t *presenter)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    esp_err_t service = pump_backend(presenter);
    presenter->last_service_error = service;
    ret = reap_ready(presenter, true);
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->completion.first_error != ESP_OK) {
        return presenter->completion.first_error;
    }
    return service;
}

void esp_display_present_engine_notify_te(esp_display_present_engine_t *presenter, uint64_t timestamp_us)
{
    if (!presenter || presenter->kind != ESP_DISPLAY_PRESENT_ENGINE_TE) {
        return;
    }
    const esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TE_EDGE,
        .timestamp_us = timestamp_us,
    };
    presenter->ops->on_event(presenter->backend, &event);
    (void)notify(presenter, false);
}

bool PRESENT_ISR_ATTR esp_display_present_engine_notify_te_isr(esp_display_present_engine_t *presenter, uint64_t timestamp_us)
{
    if (!presenter || presenter->kind != ESP_DISPLAY_PRESENT_ENGINE_TE) {
        return false;
    }
    const esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TE_EDGE,
        .timestamp_us = timestamp_us,
    };
    bool yield = presenter->event_isr(presenter->backend, &event);
    yield |= notify(presenter, true);
    return yield;
}

static esp_err_t get_caps_impl(esp_display_present_engine_t *presenter, esp_display_present_backend_caps_t *out)
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

/* Called while holding the engine gate. Store time plus one so a clock
 * reading of zero still records a wait. */
static void mark_buffer_wait(esp_display_present_engine_t *presenter, uint64_t *started)
{
    if (presenter->metrics && !*started) {
        *started = present_now_us() + 1;
    }
}

static void record_buffer_wait(esp_display_present_engine_t *presenter, uint64_t started, esp_err_t result)
{
    if (!started) {
        return;
    }
    uint64_t elapsed = present_now_us() - (started - 1);
    present_lock_enter(&presenter->control, false);
    esp_display_presenter_metrics_t *summary = &presenter->metrics->summary;
    ++summary->buffer_waits;
    summary->buffer_wait_us += elapsed;
    if (elapsed > summary->buffer_wait_max_us) {
        summary->buffer_wait_max_us = elapsed;
    }
    if (result == ESP_ERR_TIMEOUT) {
        ++summary->buffer_wait_timeouts;
    }
    present_lock_exit(&presenter->control, false);
}

static esp_err_t begin_next_frame_impl(esp_display_present_engine_t *presenter, const esp_display_present_frame_request_t *request, uint32_t timeout, bool *gate_held, uint64_t *buffer_wait)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!request) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    for (;;) {
        if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || presenter->frame.value) {
            return ESP_ERR_INVALID_STATE;
        }
        ret = reap_ready(presenter, true);
        if (ret != ESP_OK) {
            return ret;
        }
        if (presenter->completion.first_error != ESP_OK) {
            return presenter->completion.first_error;
        }
        if (presenter->completion.count < presenter->completion.limit) {
            ret = presenter->ops->begin(presenter->backend, request, &presenter->frame);
            if (ret != ESP_ERR_NO_MEM || !presenter->completion.count) {
                return ret;
            }
        }
        mark_buffer_wait(presenter, buffer_wait);
        /* Wake on any copy drain or VSYNC completion, not only head terminal:
         * post-seal async copy drain frees b->active in microseconds without
         * waiting for a full VSYNC period. */
        ret = wait_change(presenter, start, timeout, gate_held);
        if (ret != ESP_OK) {
            return ret;
        }
    }
}

static esp_err_t acquire_buffer_impl(esp_display_present_engine_t *presenter, esp_display_present_lease_t *out, uint32_t timeout, bool *gate_held, uint64_t *buffer_wait)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    for (;;) {
        if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || !presenter->frame.value || presenter->held.id.value || presenter->canceling) {
            return ESP_ERR_INVALID_STATE;
        }
        ret = refresh_fault(presenter);
        if (ret != ESP_OK || presenter->completion.first_error != ESP_OK) {
            return ret != ESP_OK ? ret : presenter->completion.first_error;
        }
        ret = presenter->ops->acquire(presenter->backend, presenter->frame, NULL, 0, &presenter->held);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        mark_buffer_wait(presenter, buffer_wait);
        ret = wait_change(presenter, start, timeout, gate_held);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ret == ESP_OK) {
        *out = presenter->held;
    }
    return ret;
}

static esp_err_t resolve_rows_impl(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                   size_t stride, uint32_t requested, uint32_t *out)
{
    if (out) {
        *out = 0;
    }
    /* Caller checked the owner and holds the gate. valid_lease rejects a lease
     * that belonged to the previous owner. */
    if (!out || !stride || !requested) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!valid_lease(presenter, lease)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t bpp = presenter->caps.draw_layout.format == ESP_DISPLAY_PRESENT_FORMAT_RGB565 ? 2 : 3;
    if (stride % bpp || stride > presenter->held.surface.capacity_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t rows = presenter->held.surface.capacity_bytes / stride;
    *out = rows < requested ? (uint32_t)rows : requested;
    if (presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_REGION && *out < requested) {
        bool quarter = presenter->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || presenter->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270;
        uint32_t a = quarter ? presenter->output.x_alignment : presenter->output.y_alignment;
        uint32_t extent = quarter ? presenter->output.w_align : presenter->output.h_align;
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

static esp_err_t submit_buffer_impl(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease,
                                    const esp_display_present_engine_area_t *area, size_t stride, uint64_t start, bool *gate_held)
{
    /* Caller already checked the owner and then took the gate. rebind cannot
     * replace the owner while this held lease still exists. */
    if (!area || !stride || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1 ||
            (uint32_t)area->x2 >= presenter->caps.draw_layout.width || (uint32_t)area->y2 >= presenter->caps.draw_layout.height) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_display_present_region_submit_t submit = { .logical_area = { (uint32_t)area->x1, (uint32_t)area->y1,
                                                                     (uint32_t)(area->x2 - area->x1) + 1, (uint32_t)(area->y2 - area->y1) + 1
                                                                   }, .stride_bytes = stride
                                                 };
    esp_err_t ret;
    for (;;) {
        if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || presenter->canceling || !valid_lease(presenter, lease)) {
            return ESP_ERR_INVALID_STATE;
        }
        ret = refresh_fault(presenter);
        if (ret != ESP_OK || presenter->completion.first_error != ESP_OK) {
            return ret != ESP_OK ? ret : presenter->completion.first_error;
        }
        ret = presenter->ops->submit_region_ex(presenter->backend, presenter->frame, presenter->held.id, &submit);
        if (ret != ESP_ERR_NO_MEM && ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(presenter, start, presenter->timeout, gate_held);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ret == ESP_OK) {
        presenter->held = (esp_display_present_lease_t) {
            0
        };
    }
    return ret;
}

static esp_err_t commit_frame_impl(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || !presenter->frame.value || presenter->canceling || presenter->held.id.value || presenter->completion.count == presenter->completion.limit) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_display_present_receipt_t receipt;
    ret = presenter->ops->end_ex(presenter->backend, presenter->frame, coverage, &receipt);
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->completion.receipts[(presenter->completion.head + presenter->completion.count) % presenter->completion.limit] = receipt;
    ++presenter->completion.count;
    ++presenter->completion.committed;
    if (presenter->metrics) {
        present_lock_enter(&presenter->control, false);
        present_metrics_t *metrics = presenter->metrics;
        if (metrics->frame_started) {
            uint64_t elapsed = present_now_us() - metrics->frame_started_us;
            uint64_t bucket = elapsed / FRAME_DURATION_BUCKET_US;
            if (bucket >= FRAME_DURATION_BUCKETS) {
                bucket = FRAME_DURATION_BUCKETS - 1;
            }
            ++metrics->frame_buckets[bucket];
            ++metrics->summary.frame_samples;
            if (elapsed > metrics->frame_max_us) {
                metrics->frame_max_us = elapsed;
            }
            metrics->frame_started = false;
        }
        if (metrics->summary.scanout_available) {
            ++metrics->pending_scanout_frames;
        }
        present_lock_exit(&presenter->control, false);
    }
    presenter->frame.value = 0;
    (void)present_event_signal(presenter->work, false);
    return ESP_OK;
}

/* Gate held. Public cancellation requires the owner. The private facade
 * bridge requires its producer session to have already revoked the frame. */
static esp_err_t cancel_frame_impl(esp_display_present_engine_t *presenter, uint32_t timeout, bool *gate_held)
{
    esp_err_t ret = ESP_OK;
    if (!presenter->frame.value) {
        return ESP_OK;
    }
    presenter->canceling = true;
    presenter->held = (esp_display_present_lease_t) {
        0
    };
    uint64_t start = present_now_ms();
    while (presenter->frame.value) {
        ret = presenter->ops->cancel_frame(presenter->backend, presenter->frame, 0);
        if (ret != ESP_ERR_TIMEOUT) {
            break;
        }
        ret = wait_change(presenter, start, timeout, gate_held);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    if (ret == ESP_OK) {
        presenter->frame.value = 0;
        presenter->canceling = false;
    }
    return ret;
}

static esp_err_t drain_impl(esp_display_present_engine_t *presenter, uint32_t timeout, bool *gate_held)
{
    /* Gate held; public callers checked owner, session handoff excludes it. */
    if (presenter->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t start = present_now_ms();
    if (presenter->completion.count) {
        (void)present_event_signal(presenter->work, false);
    }
    for (;;) {
        esp_err_t error = reap_ready(presenter, service_during_wait(presenter));
        if (error != ESP_OK || !presenter->completion.count) {
            return error != ESP_OK ? error : presenter->completion.first_error;
        }
        error = wait_change(presenter, start, timeout, gate_held);
        if (error != ESP_OK) {
            return error;
        }
    }
}

static esp_err_t get_status_impl(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *out, bool service)
{
    esp_err_t ret;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    ret = reap_ready(presenter, service);
    *out = (esp_display_present_engine_status_t) {
        .state = presenter->state,
        .frame_active = presenter->frame.value != 0,
        .cancel_pending = presenter->canceling,
        .pending_frames = presenter->completion.count,
        .committed_frames = presenter->completion.committed,
        .completed_frames = presenter->completion.completed,
        .first_error = presenter->completion.first_error,
        .last_service_error = presenter->last_service_error
    };
    return ret;
}

static esp_err_t stop_impl(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    esp_err_t ret;
    ret = presenter->ops->stop(presenter->backend, timeout);
    presenter->held = (esp_display_present_lease_t) {
        0
    };
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->frame.value = 0;
    presenter->canceling = false;
    ret = reap_ready(presenter, true);
    if (ret != ESP_OK) {
        return ret;
    }
    presenter->state = ESP_DISPLAY_PRESENT_ENGINE_STOPPED;
    return ESP_OK;
}

static esp_err_t destroy_impl(esp_display_present_engine_t *presenter)
{
    esp_err_t ret;
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_STOPPED || presenter->completion.count || presenter->frame.value) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < PRESENT_MAX_FRAMES; ++i)
        if (presenter->completion.observers[i].token) {
            return ESP_ERR_INVALID_STATE;
        }
    ret = presenter->ops->destroy(presenter->backend);
    return ret;
}

esp_err_t esp_display_present_engine_service(esp_display_present_engine_t *presenter)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    ret = service_impl(presenter);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t esp_display_present_engine_get_caps(esp_display_present_engine_t *presenter, esp_display_present_backend_caps_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    ret = get_caps_impl(presenter, out);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t present_presenter_begin_budget(esp_display_present_engine_t *presenter, const esp_display_present_frame_request_t *request, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    uint64_t buffer_wait = 0;
    ret = begin_next_frame_impl(presenter, request, remaining(start, timeout), &gate_held, &buffer_wait);
    record_buffer_wait(presenter, buffer_wait, ret);
    if (ret == ESP_OK && presenter->metrics) {
        present_lock_enter(&presenter->control, false);
        presenter->metrics->frame_started_us = present_now_us();
        presenter->metrics->frame_started = true;
        present_lock_exit(&presenter->control, false);
    }
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_request_t *request)
{
    return presenter ? present_presenter_begin_budget(presenter, request, presenter->timeout) : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_display_present_engine_acquire_buffer(esp_display_present_engine_t *presenter, esp_display_present_lease_t *out)
{
    if (out) {
        *out = (esp_display_present_lease_t) {
            0
        };
    }
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!present_mutex_take(presenter->gate, presenter->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    uint64_t buffer_wait = 0;
    ret = acquire_buffer_impl(presenter, out, remaining(start, presenter->timeout), &gate_held, &buffer_wait);
    record_buffer_wait(presenter, buffer_wait, ret);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t esp_display_present_engine_resolve_rows(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease, size_t stride, uint32_t requested, uint32_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    ret = resolve_rows_impl(presenter, lease, stride, requested, out);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t esp_display_present_engine_submit_buffer(esp_display_present_engine_t *presenter, const esp_display_present_lease_t *lease, const esp_display_present_engine_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, presenter->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    ret = submit_buffer_impl(presenter, lease, area, stride, start, &gate_held);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t present_presenter_resolve_lease_rows(esp_display_present_engine_t *presenter, esp_display_present_lease_id_t id,
                                               size_t stride, uint32_t requested, uint32_t *out)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    /* Construct a transient validation view from the authoritative held lease.
     * The caller's token still passes valid_lease; no implicit reacquisition. */
    const esp_display_present_lease_t lease = {.id = id, .surface = presenter->held.surface};
    ret = resolve_rows_impl(presenter, &lease, stride, requested, out);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t present_presenter_submit_lease(esp_display_present_engine_t *presenter, esp_display_present_lease_id_t id, void *pixels,
                                         const esp_display_present_engine_area_t *area, size_t stride)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, presenter->timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_display_present_lease_t lease = {
        .id = id,
        .surface = {.pixels = pixels, .capacity_bytes = presenter->held.surface.capacity_bytes}
    };
    bool gate_held = true;
    ret = submit_buffer_impl(presenter, &lease, area, stride, start, &gate_held);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t esp_display_present_engine_commit_frame(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    ret = commit_frame_impl(presenter, coverage);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t present_presenter_cancel_budget(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    ret = cancel_frame_impl(presenter, remaining(start, timeout), &gate_held);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t esp_display_present_engine_cancel_frame(esp_display_present_engine_t *presenter)
{
    return presenter ? present_presenter_cancel_budget(presenter, presenter->timeout) : ESP_ERR_INVALID_ARG;
}

esp_err_t present_presenter_control_finish_cancel(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    esp_err_t error = cancel_frame_impl(presenter, remaining(start, timeout), &gate_held);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return error;
}

esp_err_t esp_display_present_engine_drain(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    esp_err_t ret = check_owner(presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    ret = drain_impl(presenter, remaining(start, timeout), &gate_held);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return ret;
}

esp_err_t present_presenter_control_bind_producer(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    if (!presenter) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    if (presenter->state == ESP_DISPLAY_PRESENT_ENGINE_RUNNING && !presenter->frame.value && !presenter->held.id.value && !presenter->canceling) {
        error = drain_impl(presenter, remaining(start, timeout), &gate_held);
    }
    if (error == ESP_OK) {
        /* Drain may release the gate while waiting. Revalidate before commit. */
        if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || presenter->frame.value || presenter->held.id.value || presenter->canceling) {
            error = ESP_ERR_INVALID_STATE;
        } else {
            presenter->owner = present_current_task();
        }
    }
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return error;
}

esp_err_t present_presenter_control_poll_quiesce(esp_display_present_engine_t *presenter, uint32_t timeout,
                                                 esp_display_present_engine_receipt_t receipt,
                                                 esp_display_present_engine_status_t *out,
                                                 esp_display_present_frame_status_t *tracked)
{
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    /* The producer is paused. Async backends progress on their worker;
     * synchronous REGION has already submitted its transfers. A status poll
     * here must not enter a driver submit with an independent wait budget. */
    esp_err_t error = get_status_impl(presenter, out, false);
    if (error == ESP_OK && (out->frame_active || out->cancel_pending || out->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING)) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK && out->first_error != ESP_OK) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK && out->pending_frames) {
        (void)present_event_signal(presenter->work, false);
        error = ESP_ERR_TIMEOUT;
    }
    if (error == ESP_OK && receipt.value) {
        present_completion_observer_t *observer = present_completion_find(&presenter->completion, receipt);
        if (!observer || !observer->status.terminal) {
            error = ESP_ERR_INVALID_STATE;
        } else {
            *tracked = observer->status;
            observer->token = 0;
        }
    }
    present_mutex_give(presenter->gate);
    return error;
}

esp_err_t present_presenter_control_reset_metrics(esp_display_present_engine_t *presenter)
{
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    esp_err_t error = reap_ready(presenter, false);
    if (error != ESP_OK || presenter->completion.count || presenter->completion.first_error != ESP_OK) {
        present_mutex_give(presenter->gate);
        return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
    }
    present_metrics_t *metrics = presenter->metrics;
    if (!metrics) {
        metrics = present_alloc(sizeof(*metrics));
        if (!metrics) {
            present_mutex_give(presenter->gate);
            return ESP_ERR_NO_MEM;
        }
    }
    present_lock_enter(&presenter->control, false);
    memset(metrics, 0, sizeof(*metrics));
    metrics->summary.scanout_available = presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_FB;
    presenter->metrics = metrics;
    present_lock_exit(&presenter->control, false);
    present_mutex_give(presenter->gate);
    return ESP_OK;
}

esp_err_t present_presenter_control_get_metrics(esp_display_present_engine_t *presenter, esp_display_presenter_metrics_t *out)
{
    present_lock_enter(&presenter->control, false);
    const present_metrics_t *metrics = presenter->metrics;
    if (!metrics) {
        present_lock_exit(&presenter->control, false);
        return ESP_ERR_INVALID_STATE;
    }
    *out = metrics->summary;
    if (out->frame_samples) {
        uint64_t rank = out->frame_samples - out->frame_samples / 100;
        uint64_t count = 0;
        for (size_t i = 0; i < FRAME_DURATION_BUCKETS; ++i) {
            count += metrics->frame_buckets[i];
            if (count >= rank) {
                uint64_t upper = (i + 1) * FRAME_DURATION_BUCKET_US;
                out->frame_p99_us = i + 1 == FRAME_DURATION_BUCKETS || upper > metrics->frame_max_us ?
                                    metrics->frame_max_us : upper;
                break;
            }
        }
    }
    present_lock_exit(&presenter->control, false);
    return ESP_OK;
}

esp_err_t present_presenter_control_get_status_snapshot(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *out, bool *valid)
{
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;
    if (!presenter || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!present_mutex_take(presenter->gate, UINT32_MAX)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t ret = get_status_impl(presenter, out, true);
    *valid = true; /* get_status_impl always fills out, including reap failure. */
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t present_presenter_control_get_status(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *out)
{
    bool valid;
    return present_presenter_control_get_status_snapshot(presenter, out, &valid);
}

esp_err_t esp_display_present_engine_get_status(esp_display_present_engine_t *presenter, esp_display_present_engine_status_t *out)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? present_presenter_control_get_status(presenter, out) : error;
}

esp_err_t present_presenter_control_stop(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    esp_err_t ret = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) {
        return ret;
    }
    uint64_t start = present_now_ms();
    /* Publish closing before joining backend_worker. Private control callers have
     * stopped the producer; task-neutral observation reads this atomically. */
    esp_display_present_engine_state_t state = ESP_DISPLAY_PRESENT_ENGINE_RUNNING;
    (void)atomic_compare_exchange_strong(&presenter->state, &state, ESP_DISPLAY_PRESENT_ENGINE_CLOSING);
    if (state == ESP_DISPLAY_PRESENT_ENGINE_STOPPED) {
        return ESP_OK;
    }
    if (presenter->panel.request_abort) {
        presenter->panel.request_abort(presenter->panel.context);
    }
    request_exit(presenter);
    (void)present_event_signal(presenter->changed, false);
    if (presenter->backend_worker.handle && !present_worker_join(presenter->backend_worker.handle, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    if (!present_mutex_take(presenter->gate, remaining(start, timeout))) {
        return ESP_ERR_TIMEOUT;
    }
    ret = presenter->state == ESP_DISPLAY_PRESENT_ENGINE_STOPPED ? ESP_OK :
          stop_impl(presenter, remaining(start, timeout));
    (void)present_event_signal(presenter->changed, false);
    present_mutex_give(presenter->gate);
    return ret;
}

esp_err_t esp_display_present_engine_stop(esp_display_present_engine_t *presenter, uint32_t timeout)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? present_presenter_control_stop(presenter, timeout) : error;
}

esp_err_t present_presenter_control_destroy(esp_display_present_engine_t *presenter)
{
    esp_err_t ret = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (ret != ESP_OK) {
        return ret;
    }
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_STOPPED) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    ret = destroy_impl(presenter);
    present_mutex_give(presenter->gate);
    if (ret == ESP_OK) {
        free_presenter(presenter);
    }
    return ret;
}

esp_err_t esp_display_present_engine_destroy(esp_display_present_engine_t *presenter)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? present_presenter_control_destroy(presenter) : error;
}

esp_err_t esp_display_present_engine_commit_with_receipt(esp_display_present_engine_t *presenter, const esp_display_present_frame_coverage_t *coverage,
                                                         esp_display_present_engine_receipt_t *receipt)
{
    if (!receipt) {
        return ESP_ERR_INVALID_ARG;
    }
    receipt->value = 0;
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    present_completion_observer_t *observer = NULL;
    for (size_t i = 0; i < PRESENT_MAX_FRAMES; ++i)
        if (!presenter->completion.observers[i].token) {
            observer = &presenter->completion.observers[i];
            break;
        }
    uint64_t token = observer ? present_new_token() : 0;
    if (!token) {
        error = ESP_ERR_NO_MEM;
    } else {
        error = commit_frame_impl(presenter, coverage);
        if (error == ESP_OK) {
            *observer = (present_completion_observer_t) {
                .token = token,
                .backend = presenter->completion.receipts[(presenter->completion.head + presenter->completion.count - 1) % presenter->completion.limit],
                .status = {.sealed = true}
            };
            receipt->value = token;
        }
    }
    present_mutex_give(presenter->gate);
    return error;
}

static esp_err_t query_observer(esp_display_present_engine_t *presenter, present_completion_observer_t *observer, esp_display_present_frame_status_t *out, bool service)
{
    expire_transfers(presenter);
    return present_completion_query(observer, presenter->ops, presenter->backend, out, service);
}

esp_err_t present_presenter_control_query_frame(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt, esp_display_present_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (error != ESP_OK) {
        return error;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    error = query_observer(presenter, present_completion_find(&presenter->completion, receipt), out, true);
    present_mutex_give(presenter->gate);
    return error;
}

esp_err_t esp_display_present_engine_query_frame(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt, esp_display_present_frame_status_t *out)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? present_presenter_control_query_frame(presenter, receipt, out) : error;
}

esp_err_t esp_display_present_engine_wait_frame(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt,
                                                esp_display_present_completion_point_t point, uint32_t timeout, esp_display_present_frame_status_t *out)
{
    if (!out || !receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (point != ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED && point != ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED) {
        return ESP_ERR_INVALID_ARG;
    }
    if (point == ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED && !presenter->caps.supports_present_confirmation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    do {
        present_completion_observer_t *observer = present_completion_find(&presenter->completion, receipt);
        error = query_observer(presenter, observer, out, service_during_wait(presenter));
        if (error != ESP_OK || out->terminal) {
            break;
        }
        error = wait_change(presenter, start, timeout, &gate_held);
    } while (error == ESP_OK);
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return error;
}

static esp_err_t untrack(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt, bool require_terminal)
{
    if (!receipt.value) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = presenter ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (error != ESP_OK) {
        return error;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    present_completion_observer_t *observer = present_completion_find(&presenter->completion, receipt);
    esp_display_present_frame_status_t status;
    if (!observer) {
        error = ESP_ERR_INVALID_STATE;
    } else if (require_terminal) {
        error = query_observer(presenter, observer, &status, true);
        if (error == ESP_OK && !status.terminal) {
            error = ESP_ERR_INVALID_STATE;
        }
    }
    if (error == ESP_OK) {
        observer->token = 0;
    }
    present_mutex_give(presenter->gate);
    return error;
}

esp_err_t esp_display_present_engine_release_receipt(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? untrack(presenter, receipt, true) : error;
}

esp_err_t esp_display_present_engine_cancel_tracking(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt)
{
    esp_err_t error = check_owner(presenter);
    return error == ESP_OK ? untrack(presenter, receipt, false) : error;
}

esp_err_t esp_display_present_engine_set_rotation(esp_display_present_engine_t *presenter, esp_display_present_engine_rotation_t rotation, uint32_t timeout)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (rotation < ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0 || rotation > ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!presenter->ops->set_rotation) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    uint64_t start = present_now_ms();
    if (!present_mutex_take(presenter->gate, timeout)) {
        return ESP_ERR_TIMEOUT;
    }
    bool gate_held = true;
    if (presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING || presenter->frame.value || presenter->held.id.value) {
        error = ESP_ERR_INVALID_STATE;
    } else {
        error = drain_impl(presenter, remaining(start, timeout), &gate_held);
    }
    if (error == ESP_OK && presenter->state != ESP_DISPLAY_PRESENT_ENGINE_RUNNING) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK) {
        error = presenter->ops->set_rotation(presenter->backend, rotation);
    }
    if (error == ESP_OK) {
        presenter->rotation = rotation;
        error = presenter->ops->get_caps(presenter->backend, &presenter->caps);
    }
    if (gate_held) {
        present_mutex_give(presenter->gate);
    }
    return error;
}

esp_err_t esp_display_present_engine_resolve_render_area(esp_display_present_engine_t *presenter, const esp_display_present_rect_t *dirty, esp_display_present_rect_t *render)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    esp_display_present_resolved_plan_t plan = {
        .logical_layout = presenter->caps.draw_layout,
        .output = presenter->output,
    };
    plan.normalized.backend.rotation = presenter->rotation;
    plan.normalized.backend.render_contract = presenter->caps.contract;
    plan.images.count = presenter->kind == ESP_DISPLAY_PRESENT_ENGINE_REGION ? 0 : 1;
    plan.physical_layout = plan.logical_layout;
    if (presenter->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90 || presenter->rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270) {
        plan.physical_layout.width = plan.logical_layout.height;
        plan.physical_layout.height = plan.logical_layout.width;
    }
    esp_display_present_rect_t physical;
    error = esp_display_present_plan_render_area(&plan, dirty, render, &physical);
    present_mutex_give(presenter->gate);
    return error;
}

size_t present_presenter_memory(const esp_display_present_engine_config_t *config)
{
    return sizeof(esp_display_present_engine_t) + present_transfer_memory(config->transfer_timeout_ms) + present_mutex_memory() + 2 * present_event_memory() +
           (config->background_service ? present_worker_memory() : 0);
}

esp_err_t esp_display_present_engine_get_rotation(esp_display_present_engine_t *presenter, esp_display_present_engine_rotation_t *out)
{
    esp_err_t error = check_owner(presenter);
    if (error != ESP_OK) {
        return error;
    }
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
    *out = presenter->rotation;
    present_mutex_give(presenter->gate);
    return ESP_OK;
}

/* Control-plane observation owns no producer lease. Caller excludes deletion. */
esp_err_t present_presenter_control_release(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt)
{
    return untrack(presenter, receipt, true);
}

esp_err_t present_presenter_control_cancel_tracking(esp_display_present_engine_t *presenter, esp_display_present_engine_receipt_t receipt)
{
    return untrack(presenter, receipt, false);
}

#ifdef PRESENT_HOST
void present_presenter_test_gate_lock(esp_display_present_engine_t *presenter)
{
    (void)present_mutex_take(presenter->gate, UINT32_MAX);
}

void present_presenter_test_gate_unlock(esp_display_present_engine_t *presenter)
{
    present_mutex_give(presenter->gate);
}
#endif
