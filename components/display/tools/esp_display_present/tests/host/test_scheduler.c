/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "core/present_engine.h"
#include "pixel/present_pixel_ops.h"
#include "../../src/platform/present_os.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define OK(x) assert((x) == ESP_OK)

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    esp_display_present_panel_event_fn callback;
    void *user;
    esp_display_present_surface_t surface;
    uint64_t cookie;
    unsigned submissions;
    unsigned quiesces;
    unsigned aborts;
    bool blocked;
    bool release;
    bool reject;
    bool immediate;
    bool uncertain;
    bool fb;
    bool copy_entered;
} panel_t;
static void complete_locked(panel_t *p)
{
    assert(p->callback && p->cookie);
    uint16_t value;
    memcpy(&value, p->surface.pixels, sizeof(value));
    assert(value == 0xabab);
    esp_display_present_backend_event_t event = {.kind = p->fb ? ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED : ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE,
                                                 .transaction_cookie = p->cookie
                                                };
    p->cookie = 0;
    (void)p->callback(p->user, &event, false);
}

static esp_err_t attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    panel_t *p = context;
    p->callback = callback;
    p->user = user;
    return ESP_OK;
}

static esp_display_present_tx_result_t submit(void *context, const esp_display_present_surface_t *surface, const esp_display_present_rect_t *area, uint64_t cookie)
{
    (void)area;
    panel_t *p = context;
    pthread_mutex_lock(&p->lock);
    assert(!p->cookie);
    p->surface = *surface;
    p->submissions++;
    pthread_cond_broadcast(&p->condition);
    while (p->blocked && !p->release) {
        pthread_cond_wait(&p->condition, &p->lock);
    }
    bool reject = p->reject;
    bool uncertain = p->uncertain;
    if (!reject) {
        p->cookie = cookie;
        if (p->immediate) {
            complete_locked(p);
        }
    }
    pthread_mutex_unlock(&p->lock);
    return (esp_display_present_tx_result_t) {
        reject ? ESP_DISPLAY_PRESENT_TX_REJECTED : uncertain ? ESP_DISPLAY_PRESENT_TX_UNCERTAIN : ESP_DISPLAY_PRESENT_TX_ACCEPTED,
               reject || uncertain ? ESP_FAIL : ESP_OK
    };
}

static esp_err_t quiesce(void *context, uint32_t timeout)
{
    (void)timeout;
    panel_t *p = context;
    pthread_mutex_lock(&p->lock);
    p->quiesces++;
    esp_err_t result = p->cookie ? ESP_ERR_TIMEOUT : ESP_OK;
    if (result == ESP_OK) {
        p->callback = NULL;
    }
    pthread_mutex_unlock(&p->lock);
    return result;
}

static void request_abort(void *context)
{
    panel_t *p = context;
    pthread_mutex_lock(&p->lock);
    ++p->aborts;
    pthread_cond_broadcast(&p->condition);
    pthread_mutex_unlock(&p->lock);
}

static void init_panel(panel_t *p)
{
    *p = (panel_t) {
        0
    };
    assert(pthread_mutex_init(&p->lock, NULL) == 0);
    pthread_condattr_t attr;
    assert(pthread_condattr_init(&attr) == 0);
    assert(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0);
    assert(pthread_cond_init(&p->condition, &attr) == 0);
    pthread_condattr_destroy(&attr);
}

static void wait_submit(panel_t *p, unsigned count)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&p->lock);
    while (p->submissions < count) {
        assert(pthread_cond_timedwait(&p->condition, &p->lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&p->lock);
}

static esp_display_present_engine_t *create(panel_t *panel, bool fb)
{
    panel->fb = fb;
    esp_display_present_engine_config_t config = {.backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = fb ? 3 : 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .panel = {panel, attach, submit, quiesce},
            .te_window_us = 1000000, .te_budget_us = 1000
        },
        .backend_kind = fb ? ESP_DISPLAY_PRESENT_ENGINE_FB : ESP_DISPLAY_PRESENT_ENGINE_TE,
        .timeout_ms = 1000, .background_service = true
    };
    config.backend.panel.request_abort = request_abort;
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&config, &p));
    return p;
}

static void frame(esp_display_present_engine_t *p)
{
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    for (int y = 0; y < 4; y += 2) {
        esp_display_present_lease_t lease;
        OK(esp_display_present_engine_acquire_buffer(p, &lease));
        memset(lease.surface.pixels, 0xab, 16);
        esp_display_present_engine_area_t area = {0, y, 3, y + 1};
        OK(esp_display_present_engine_submit_buffer(p, &lease, &area, 8));
    }
    OK(esp_display_present_engine_commit_frame(p, NULL));
}

static void complete(panel_t *p)
{
    pthread_mutex_lock(&p->lock);
    complete_locked(p);
    pthread_mutex_unlock(&p->lock);
}

static void destroy(esp_display_present_engine_t *presenter, panel_t *panel)
{
    OK(esp_display_present_engine_stop(presenter, 1000));
    OK(esp_display_present_engine_destroy(presenter));
    pthread_cond_destroy(&panel->condition);
    pthread_mutex_destroy(&panel->lock);
}

static void autonomous(bool fb)
{
    panel_t panel;
    init_panel(&panel);
    esp_display_present_engine_t *p = create(&panel, fb);
    frame(p);
    if (!fb) {
        esp_display_present_engine_notify_te_isr(p, present_now_us());
    }
    wait_submit(&panel, 1); /* Owner does not pump/query/drain to cause submission. */
    assert(esp_display_present_engine_drain(p, 0) == ESP_ERR_TIMEOUT);
    if (fb) {
        frame(p);    /* Third buffer can queue while first is pending. */
    }
    complete(&panel);
    if (fb) {
        wait_submit(&panel, 2);
        complete(&panel);
    }
    OK(esp_display_present_engine_drain(p, 1000));
    esp_display_present_engine_status_t status;
    OK(esp_display_present_engine_get_status(p, &status));
    assert(status.completed_frames == (fb ? 2u : 1u));
    pthread_mutex_lock(&panel.lock);
    panel.immediate = true;
    pthread_mutex_unlock(&panel.lock);
    frame(p);
    if (!fb) {
        esp_display_present_engine_notify_te(p, present_now_us());
    }
    wait_submit(&panel, fb ? 3 : 2);
    OK(esp_display_present_engine_drain(p, 1000));
    destroy(p, &panel);
}

static _Thread_local esp_display_present_engine_t *wake_engine_on_wait;
static _Thread_local panel_t *wake_panel_on_wait;
bool __real_present_event_wait(present_os_event_t *, uint32_t);
bool __wrap_present_event_wait(present_os_event_t *event, uint32_t timeout)
{
    if (wake_panel_on_wait) {
        panel_t *panel = wake_panel_on_wait;
        wake_panel_on_wait = NULL;
        esp_display_present_engine_notify_te(wake_engine_on_wait, present_now_us());
        wait_submit(panel, 1); /* Worker has taken the gate while the caller waits. */
    }
    return __real_present_event_wait(event, timeout);
}

static void bounded_join(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.blocked = true;
    esp_display_present_engine_t *p = create(&panel, false);
    frame(p);
    wake_engine_on_wait = p;
    wake_panel_on_wait = &panel;
    uint64_t start = present_now_ms();
    assert(esp_display_present_engine_drain(p, 25) == ESP_ERR_TIMEOUT);
    assert(!wake_panel_on_wait && present_now_ms() - start < 500);
    start = present_now_ms();
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(present_now_ms() - start < 500);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    esp_display_present_frame_request_t request = {0};
    assert(esp_display_present_engine_begin_next_frame(p, &request) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&panel.lock);
    assert(panel.quiesces == 0); /* No quiesce racing submit. */
    panel.release = true;
    panel.immediate = true;
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    destroy(p, &panel);
}

static void rejection(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.reject = true;
    esp_display_present_engine_t *p = create(&panel, false);
    frame(p);
    esp_display_present_engine_notify_te(p, present_now_us());
    wait_submit(&panel, 1);
    esp_display_present_engine_status_t status;
    OK(esp_display_present_engine_get_status(p, &status));
    /* A queued copy-progress wake may already have run a no-op service after
     * the rejected submit. last_service_error is a snapshot, not a sticky
     * rejection history. The driver is still rejecting and the frame must
     * remain pending without poisoning the accepted-operation fault. */
    assert(status.last_service_error == ESP_FAIL || status.last_service_error == ESP_OK);
    assert(status.first_error == ESP_OK && status.pending_frames == 1);
    pthread_mutex_lock(&panel.lock);
    panel.reject = false;
    pthread_mutex_unlock(&panel.lock);
    esp_display_present_engine_notify_te_isr(p, present_now_us());
    wait_submit(&panel, 2);
    complete(&panel);
    OK(esp_display_present_engine_drain(p, 1000));
    destroy(p, &panel);
}

static esp_display_present_engine_t *create_gram(panel_t *panel, uint32_t transfer_timeout)
{
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_REGION,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2,
            .max_frames = 4, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .transfer_capacity = 1, .deferred_submit = true, .panel = {panel, attach, submit, quiesce}
        },
        .timeout_ms = 20, .transfer_timeout_ms = transfer_timeout, .background_service = true
    };
    esp_display_present_engine_t *p = NULL;
    OK(esp_display_present_engine_create(&config, &p));
    return p;
}

static esp_display_present_lease_t gram_tile(esp_display_present_engine_t *p, unsigned y)
{
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(p, &lease));
    memset(lease.surface.pixels, 0xab, 16);
    esp_display_present_engine_area_t area = {0, y, 3, y + 1};
    OK(esp_display_present_engine_submit_buffer(p, &lease, &area, 8));
    return lease;
}

static esp_err_t stalled_copy(void *context, const esp_display_present_copy_request_t *request)
{
    panel_t *panel = context;
    pthread_mutex_lock(&panel->lock);
    panel->copy_entered = true;
    pthread_cond_broadcast(&panel->condition);
    while (!panel->release) {
        pthread_cond_wait(&panel->condition, &panel->lock);
    }
    pthread_mutex_unlock(&panel->lock);
    return esp_display_present_pixel_execute_cpu(request);
}

static void shared_copy_bounded_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.fb = true;
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB,
        .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .tile_count = 1, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .async_copy = true, .copy_on_backend_worker = true,
            .copy_execute = stalled_copy, .copy_user = &panel,
            .panel = {&panel, attach, submit, quiesce},
        },
        .timeout_ms = 5, .background_service = true,
    };
    esp_display_present_engine_t *p = NULL;
    OK(esp_display_present_engine_create(&config, &p));
    esp_display_present_frame_request_t request = {.full_redraw = true};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    esp_display_present_lease_t first = gram_tile(p, 0);
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&panel.lock);
    while (!panel.copy_entered) {
        assert(pthread_cond_timedwait(&panel.condition, &panel.lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&panel.lock);
    /* Simulate a missing hardware callback. No caller may reuse the source,
     * but the executor must not hold the owner gate or defeat time budgets. */
    uint64_t start = present_now_ms();
    esp_display_present_lease_t unavailable;
    assert(esp_display_present_engine_acquire_buffer(p, &unavailable) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_cancel_frame(p) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(present_now_ms() - start < 500);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    assert(esp_display_present_engine_stop(p, 0) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_acquire_buffer(p, &unavailable) == ESP_ERR_INVALID_STATE);
    for (unsigned i = 0; i < 16; ++i) {
        assert(((const unsigned char *)first.surface.pixels)[i] == 0xab);
    }
    pthread_mutex_lock(&panel.lock);
    assert(panel.quiesces == 0 && panel.submissions == 0);
    panel.release = true; /* Late callback: retained buffers are still valid. */
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    OK(esp_display_present_engine_stop(p, 1000));
    assert(panel.submissions == 0);
    destroy(p, &panel);
}

static void gram_driver_block_and_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.blocked = true;
    esp_display_present_engine_t *p = create_gram(&panel, 0);
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    esp_display_present_lease_t first = gram_tile(p, 0);
    wait_submit(&panel, 1);
    uint64_t start = present_now_ms();
    esp_display_present_lease_t second = gram_tile(p, 2);
    assert(present_now_ms() - start < 500);
    assert(first.surface.pixels != second.surface.pixels);
    OK(esp_display_present_engine_commit_frame(p, NULL));
    pthread_mutex_lock(&panel.lock);
    assert(panel.surface.pixels == first.surface.pixels); /* No conversion copy. */
    pthread_mutex_unlock(&panel.lock);
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&panel.lock);
    assert(panel.quiesces == 0 && panel.submissions == 1);
    panel.release = true;
    panel.immediate = true;
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    OK(esp_display_present_engine_stop(p, 1000));
    assert(panel.submissions == 1); /* Closing does not submit queued tile #2. */
    destroy(p, &panel);
}

static void gram_zero_copy_backpressure(void)
{
    panel_t panel;
    init_panel(&panel);
    esp_display_present_engine_t *p = create_gram(&panel, 0);
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    esp_display_present_lease_t first = gram_tile(p, 0);
    wait_submit(&panel, 1);
    (void)gram_tile(p, 2);
    esp_display_present_lease_t unavailable;
    assert(esp_display_present_engine_acquire_buffer(p, &unavailable) == ESP_ERR_TIMEOUT);
    OK(esp_display_present_engine_commit_frame(p, NULL));
    pthread_mutex_lock(&panel.lock);
    assert(panel.surface.pixels == first.surface.pixels && panel.submissions == 1);
    pthread_mutex_unlock(&panel.lock);
    complete(&panel);
    wait_submit(&panel, 2);
    complete(&panel);
    OK(esp_display_present_engine_drain(p, 1000));
    pthread_mutex_lock(&panel.lock);
    panel.immediate = true;
    pthread_mutex_unlock(&panel.lock);
    frame(p); /* Both callbacks may occur before submit returns. */
    OK(esp_display_present_engine_drain(p, 1000));
    destroy(p, &panel);
}

static void gram_uncertain_keeps_pixels(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.uncertain = true;
    esp_display_present_engine_t *p = create_gram(&panel, 0);
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    esp_display_present_lease_t source = gram_tile(p, 0);
    wait_submit(&panel, 1);
    esp_display_present_engine_status_t status;
    uint64_t start = present_now_ms();
    do {
        OK(esp_display_present_engine_get_status(p, &status));
        assert(present_now_ms() - start < 1000);
        present_pause();
    } while (status.first_error == ESP_OK);
    assert(status.first_error == ESP_FAIL);
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&panel.lock);
    assert(panel.surface.pixels == source.surface.pixels);
    assert(*(const uint16_t *)panel.surface.pixels == 0xabab);
    pthread_mutex_unlock(&panel.lock);
    complete(&panel); /* Real completion, rather than submit error, ends reads. */
    destroy(p, &panel);
}

static void gram_timeout_during_driver_call(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.blocked = true;
    esp_display_present_engine_t *p = create_gram(&panel, 5);
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &request));
    esp_display_present_lease_t source = gram_tile(p, 0);
    wait_submit(&panel, 1);
    esp_display_present_engine_status_t status;
    uint64_t start = present_now_ms();
    do {
        OK(esp_display_present_engine_get_status(p, &status));
        assert(present_now_ms() - start < 1000);
        present_pause();
    } while (status.first_error == ESP_OK);
    assert(status.first_error == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    pthread_mutex_lock(&panel.lock);
    assert(panel.quiesces == 0 && panel.surface.pixels == source.surface.pixels);
    assert(*(const uint16_t *)panel.surface.pixels == 0xabab);
    panel.release = panel.immediate = true;
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    destroy(p, &panel);
}

/* TE driver calls may block; the worker must lend the engine gate just as
 * REGION does, so foreground status and bounded stop remain usable. */
static void te_driver_block_and_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.blocked = true;
    esp_display_present_engine_t *p = create(&panel, false);
    frame(p);
    esp_display_present_engine_notify_te_isr(p, present_now_us());
    wait_submit(&panel, 1);
    esp_display_present_engine_status_t status;
    OK(esp_display_present_engine_get_status(p, &status));
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&panel.lock);
    assert(panel.quiesces == 0 && panel.submissions == 1 && panel.aborts >= 1);
    assert(*(const uint16_t *)panel.surface.pixels == 0xabab);
    panel.release = panel.immediate = true;
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    destroy(p, &panel);
}

static esp_display_present_engine_t *create_shared_te(panel_t *panel, unsigned tiles, bool stalled)
{
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_TE,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = tiles,
            .compose_count = 1, .max_frames = 4, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .async_copy = true, .copy_on_backend_worker = true,
            .copy_execute = stalled ? stalled_copy : NULL, .copy_user = panel,
            .te_window_us = 1000000, .te_budget_us = 1000,
            .panel = {panel, attach, submit, quiesce, request_abort},
        },
        .timeout_ms = stalled ? 5 : 1000, .background_service = true,
    };
    esp_display_present_engine_t *engine = NULL;
    OK(esp_display_present_engine_create(&config, &engine));
    return engine;
}

/* Both input pool sizes retain async copy, including runtime rotation and
 * a driver call that blocks while foreground APIs and bounded stop run. */
/* Model recurring GPIO edges: asynchronous composition may become READY after
 * commit returns, so a single earlier edge must not be treated as admission. */
static void wait_shared_te_submit(esp_display_present_engine_t *engine, panel_t *panel, unsigned count)
{
    uint64_t start = present_now_ms();
    for (;;) {
        pthread_mutex_lock(&panel->lock);
        bool ready = panel->submissions >= count;
        pthread_mutex_unlock(&panel->lock);
        if (ready) {
            return;
        }
        assert(present_now_ms() - start < 2000);
        esp_display_present_engine_notify_te_isr(engine, present_now_us());
        present_pause();
    }
}

static void shared_te_pipeline(void)
{
    for (unsigned tiles = 1; tiles <= 2; ++tiles) {
        panel_t panel;
        init_panel(&panel);
        esp_display_present_engine_t *engine = create_shared_te(&panel, tiles, false);
        frame(engine);
        esp_display_present_engine_notify_te_isr(engine, present_now_us());
        wait_shared_te_submit(engine, &panel, 1);
        complete(&panel);
        OK(esp_display_present_engine_drain(engine, 1000));
        OK(esp_display_present_engine_set_rotation(engine, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 1000));
        pthread_mutex_lock(&panel.lock);
        panel.blocked = true;
        pthread_mutex_unlock(&panel.lock);
        frame(engine);
        esp_display_present_engine_notify_te_isr(engine, present_now_us());
        wait_shared_te_submit(engine, &panel, 2);
        esp_display_present_engine_status_t status;
        OK(esp_display_present_engine_get_status(engine, &status));
        assert(esp_display_present_engine_stop(engine, 5) == ESP_ERR_TIMEOUT);
        assert(esp_display_present_engine_destroy(engine) == ESP_ERR_INVALID_STATE);
        pthread_mutex_lock(&panel.lock);
        assert(panel.aborts && !panel.quiesces);
        panel.release = panel.immediate = true;
        pthread_cond_broadcast(&panel.condition);
        pthread_mutex_unlock(&panel.lock);
        destroy(engine, &panel);
    }
}

static void shared_te_cancel_copy_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    esp_display_present_engine_t *engine = create_shared_te(&panel, 1, true);
    esp_display_present_frame_request_t request = {.full_redraw = true};
    OK(esp_display_present_engine_begin_next_frame(engine, &request));
    esp_display_present_lease_t source = gram_tile(engine, 0);
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&panel.lock);
    while (!panel.copy_entered) {
        assert(pthread_cond_timedwait(&panel.condition, &panel.lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&panel.lock);
    assert(esp_display_present_engine_cancel_frame(engine) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_stop(engine, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(engine) == ESP_ERR_INVALID_STATE);
    assert(*(const uint16_t *)source.surface.pixels == 0xabab);
    pthread_mutex_lock(&panel.lock);
    assert(panel.submissions == 0 && panel.quiesces == 0);
    panel.release = true;
    pthread_cond_broadcast(&panel.condition);
    pthread_mutex_unlock(&panel.lock);
    destroy(engine, &panel);
}

static void wait_te_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    esp_display_present_engine_t *p = create(&panel, false);
    frame(p); /* No TE: worker must park, and stop must wake it. */
    destroy(p, &panel);
    assert(panel.submissions == 0);
}

static void failure_cleanup(void)
{
    panel_t panel;
    init_panel(&panel);
    esp_display_present_engine_config_t config = {.background_service = true, .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_TE,
                                                  .backend = {.panel = {&panel, attach, submit, quiesce}}
                                                 };
    esp_display_present_engine_t *p;
    assert(esp_display_present_engine_create(&config, &p) == ESP_ERR_INVALID_ARG && p == NULL);
    assert(panel.callback == NULL);
    pthread_cond_destroy(&panel.condition);
    pthread_mutex_destroy(&panel.lock);
}
typedef struct {
    panel_t panel;
    unsigned copy_calls;
    unsigned stall_on_call;
    bool copy_stalled;
} fb_repair_probe_t;

static esp_err_t probe_repair_copy(void *context, const esp_display_present_copy_request_t *request)
{
    fb_repair_probe_t *probe = context;
    pthread_mutex_lock(&probe->panel.lock);
    unsigned call = ++probe->copy_calls;
    if (probe->stall_on_call && call == probe->stall_on_call) {
        probe->copy_stalled = true;
        pthread_cond_broadcast(&probe->panel.condition);
        while (!probe->panel.release) {
            pthread_cond_wait(&probe->panel.condition, &probe->panel.lock);
        }
    }
    pthread_mutex_unlock(&probe->panel.lock);
    return esp_display_present_pixel_execute_cpu(request);
}

static void shared_fb_multi_repair_drain_and_stop(void)
{
    fb_repair_probe_t probe = {0};
    init_panel(&probe.panel);
    probe.panel.fb = true;
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB,
        .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 1, .buffer_count = 2, .tile_count = 1, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .async_copy = true, .copy_on_backend_worker = true,
            .copy_execute = probe_repair_copy, .copy_user = &probe,
            .panel = {&probe.panel, attach, submit, quiesce},
        },
        .timeout_ms = 5, .background_service = true,
    };
    esp_display_present_engine_t *p = NULL;
    OK(esp_display_present_engine_create(&config, &p));

    /* Frame 1: full redraw establishes baseline coverage. */
    esp_display_present_frame_request_t full_req = {.full_redraw = true};
    OK(esp_display_present_engine_begin_next_frame(p, &full_req));
    for (int y = 0; y < 4; ++y) {
        esp_display_present_lease_t lease;
        OK(esp_display_present_engine_acquire_buffer(p, &lease));
        memset(lease.surface.pixels, 0xab, 8);
        esp_display_present_engine_area_t area = {0, y, 3, y};
        OK(esp_display_present_engine_submit_buffer(p, &lease, &area, 8));
    }
    OK(esp_display_present_engine_commit_frame(p, NULL));
    wait_submit(&probe.panel, 1);
    complete(&probe.panel);
    OK(esp_display_present_engine_drain(p, 1000));

    /* Frame 2: center 1x1 update leaves 4 separate repair spans to drain. */
    unsigned before_copies = probe.copy_calls;
    esp_display_present_frame_request_t partial_req = {0};
    OK(esp_display_present_engine_begin_next_frame(p, &partial_req));
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(p, &lease));
    memset(lease.surface.pixels, 0xab, 8);
    esp_display_present_engine_area_t center = {1, 1, 1, 1};
    OK(esp_display_present_engine_submit_buffer(p, &lease, &center, 8));
    OK(esp_display_present_engine_commit_frame(p, NULL));
    wait_submit(&probe.panel, 2);
    complete(&probe.panel);
    OK(esp_display_present_engine_drain(p, 1000));
    assert(probe.copy_calls - before_copies >= 4);

    /* Frame 3: write a different pixel so slot 0 still needs Frame 2's
     * center pixel repaired, then stall mid-repair and verify cancel/stop. */
    pthread_mutex_lock(&probe.panel.lock);
    probe.stall_on_call = probe.copy_calls + 2; /* 1 tile copy + 1st repair */
    probe.copy_stalled = false;
    probe.panel.release = false;
    pthread_mutex_unlock(&probe.panel.lock);
    OK(esp_display_present_engine_begin_next_frame(p, &partial_req));
    OK(esp_display_present_engine_acquire_buffer(p, &lease));
    memset(lease.surface.pixels, 0xab, 8);
    esp_display_present_engine_area_t corner = {0, 0, 0, 0};
    OK(esp_display_present_engine_submit_buffer(p, &lease, &corner, 8));
    OK(esp_display_present_engine_commit_frame(p, NULL));
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&probe.panel.lock);
    while (!probe.copy_stalled) {
        assert(pthread_cond_timedwait(&probe.panel.condition, &probe.panel.lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&probe.panel.lock);
    assert(esp_display_present_engine_drain(p, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&probe.panel.lock);
    probe.panel.release = true;
    probe.panel.immediate = true;
    pthread_cond_broadcast(&probe.panel.condition);
    pthread_mutex_unlock(&probe.panel.lock);
    destroy(p, &probe.panel);
}

static void shared_te_cancel_midframe(void)
{
    for (unsigned tiles = 1; tiles <= 2; ++tiles) {
        panel_t panel;
        init_panel(&panel);
        esp_display_present_engine_t *engine = create_shared_te(&panel, tiles, false);
        /* Partial frame canceled before commit: worker stays clean for next frame. */
        esp_display_present_frame_request_t req = {0};
        OK(esp_display_present_engine_begin_next_frame(engine, &req));
        (void)gram_tile(engine, 0);
        OK(esp_display_present_engine_cancel_frame(engine));
        frame(engine);
        esp_display_present_engine_notify_te_isr(engine, present_now_us());
        wait_shared_te_submit(engine, &panel, 1);
        complete(&panel);
        OK(esp_display_present_engine_drain(engine, 1000));
        destroy(engine, &panel);
    }
}

static void split_te_repair_stalled_stop(void)
{
    fb_repair_probe_t probe = {0};
    init_panel(&probe.panel);
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_TE,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 1,
            .compose_count = 2, .max_frames = 4, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .async_copy = true, .copy_on_backend_worker = false,
            .copy_execute = probe_repair_copy, .copy_user = &probe,
            .te_window_us = 1000000, .te_budget_us = 1000,
            .panel = {&probe.panel, attach, submit, quiesce, request_abort},
        },
        .timeout_ms = 5, .background_service = true, .worker_priority = 7,
    };
    esp_display_present_engine_t *engine = NULL;
    OK(esp_display_present_engine_create(&config, &engine));
    frame(engine);
    esp_display_present_engine_notify_te_isr(engine, present_now_us());
    wait_shared_te_submit(engine, &probe.panel, 1);
    complete(&probe.panel);
    OK(esp_display_present_engine_drain(engine, 1000));

    /* Frame 2 writes only top half into candidate #1; sequential repair from baseline #0 stalls. */
    pthread_mutex_lock(&probe.panel.lock);
    probe.stall_on_call = probe.copy_calls + 2;
    probe.copy_stalled = false;
    probe.panel.release = false;
    pthread_mutex_unlock(&probe.panel.lock);
    esp_display_present_frame_request_t req = {0};
    OK(esp_display_present_engine_begin_next_frame(engine, &req));
    (void)gram_tile(engine, 0);
    OK(esp_display_present_engine_commit_frame(engine, NULL));

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&probe.panel.lock);
    while (!probe.copy_stalled) {
        assert(pthread_cond_timedwait(&probe.panel.condition, &probe.panel.lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&probe.panel.lock);
    assert(esp_display_present_engine_drain(engine, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_stop(engine, 5) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_destroy(engine) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&probe.panel.lock);
    probe.panel.release = true;
    probe.panel.immediate = true;
    pthread_cond_broadcast(&probe.panel.condition);
    pthread_mutex_unlock(&probe.panel.lock);
    destroy(engine, &probe.panel);
}

static void fb_full_runtime_rotation_roundtrip(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.fb = true;
    esp_display_present_engine_config_t config = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB,
        .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 4, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .active_writes = true,
            .async_copy = true, .panel = {&panel, attach, submit, quiesce, request_abort},
        },
        .timeout_ms = 1000, .background_service = true, .worker_priority = 7,
    };
    esp_display_present_engine_t *p = NULL;
    OK(esp_display_present_engine_create(&config, &p));
    for (unsigned step = 0; step < 3; ++step) {
        if (step == 1) {
            OK(esp_display_present_engine_set_rotation(p, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90, 1000));
        } else if (step == 2) {
            OK(esp_display_present_engine_set_rotation(p, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1000));
        }
        esp_display_present_frame_request_t req = {.full_redraw = true};
        OK(esp_display_present_engine_begin_next_frame(p, &req));
        esp_display_present_lease_t lease;
        OK(esp_display_present_engine_acquire_buffer(p, &lease));
        memset(lease.surface.pixels, 0xab, 32);
        esp_display_present_engine_area_t full = {0, 0, 3, 3};
        OK(esp_display_present_engine_submit_buffer(p, &lease, &full, 8));
        OK(esp_display_present_engine_commit_frame(p, NULL));
        wait_submit(&panel, step + 1);
        complete(&panel);
        OK(esp_display_present_engine_drain(p, 1000));
    }
    destroy(p, &panel);
}

int main(void)
{
    autonomous(false);
    autonomous(true);
    bounded_join();
    rejection();
    shared_copy_bounded_stop();
    shared_fb_multi_repair_drain_and_stop();
    fb_full_runtime_rotation_roundtrip();
    gram_driver_block_and_stop();
    gram_zero_copy_backpressure();
    gram_uncertain_keeps_pixels();
    gram_timeout_during_driver_call();
    te_driver_block_and_stop();
    shared_te_pipeline();
    shared_te_cancel_midframe();
    split_te_repair_stalled_stop();
    shared_te_cancel_copy_stop();
    wait_te_stop();
    failure_cleanup();
    puts("Scheduler: autonomous TE/FB, queued FB, early callback, bounded join and parked exit passed");
    return 0;
}
