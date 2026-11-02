/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_presenter.h"
#include "../src/present_os.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define OK(x) assert((x) == ESP_OK)

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pv2_panel_event_fn callback;
    void *user;
    pv2_surface_t surface;
    uint64_t cookie;
    unsigned submissions;
    unsigned quiesces;
    bool blocked;
    bool release;
    bool reject;
    bool immediate;
    bool fb;
} panel_t;
static void complete_locked(panel_t *p)
{
    assert(p->callback && p->cookie);
    uint16_t value;
    memcpy(&value, p->surface.pixels, sizeof(value));
    assert(value == 0xabab);
    pv2_backend_event_t event = {.kind = p->fb ? PV2_EVENT_SCANOUT_CONFIRMED : PV2_EVENT_TRANSFER_DONE,
                                 .transaction_cookie = p->cookie
                                };
    p->cookie = 0;
    (void)p->callback(p->user, &event, false);
}

static esp_err_t attach(void *context, pv2_panel_event_fn callback, void *user)
{
    panel_t *p = context;
    p->callback = callback;
    p->user = user;
    return ESP_OK;
}

static pv2_tx_result_t submit(void *context, const pv2_surface_t *surface, const pv2_rect_t *area, uint64_t cookie)
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
    if (!reject) {
        p->cookie = cookie;
        if (p->immediate) {
            complete_locked(p);
        }
    }
    pthread_mutex_unlock(&p->lock);
    return (pv2_tx_result_t) {
        reject ? PV2_TX_REJECTED : PV2_TX_ACCEPTED, reject ? ESP_FAIL : ESP_OK
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

static pv2_presenter_t *create(panel_t *panel, bool fb)
{
    panel->fb = fb;
    pv2_presenter_config_t config = {.backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = fb ? 3 : 2, .max_frames = 4,
            .format = PV2_FORMAT_RGB565, .panel = {panel, attach, submit, quiesce},
            .te_window_us = 1000000, .te_transfer_budget_us = 1000
        },
        .backend_kind = fb ? PV2_PRESENTER_FB : PV2_PRESENTER_TE,
        .timeout_ms = 1000, .background_service = true
    };
    pv2_presenter_t *p;
    OK(pv2_presenter_create(&config, &p));
    return p;
}

static void frame(pv2_presenter_t *p)
{
    pv2_frame_request_t request = {0};
    OK(pv2_presenter_begin_next_frame(p, &request));
    for (int y = 0; y < 4; y += 2) {
        pv2_lease_t lease;
        OK(pv2_presenter_acquire_buffer(p, &lease));
        memset(lease.surface.pixels, 0xab, 16);
        pv2_area_t area = {0, y, 3, y + 1};
        OK(pv2_presenter_submit_buffer(p, &lease, &area, 8));
    }
    OK(pv2_presenter_commit_frame(p, NULL));
}

static void complete(panel_t *p)
{
    pthread_mutex_lock(&p->lock);
    complete_locked(p);
    pthread_mutex_unlock(&p->lock);
}

static void destroy(pv2_presenter_t *presenter, panel_t *panel)
{
    OK(pv2_presenter_stop(presenter, 1000));
    OK(pv2_presenter_destroy(presenter));
    pthread_cond_destroy(&panel->condition);
    pthread_mutex_destroy(&panel->lock);
}

static void autonomous(bool fb)
{
    panel_t panel;
    init_panel(&panel);
    pv2_presenter_t *p = create(&panel, fb);
    frame(p);
    if (!fb) {
        pv2_presenter_notify_te_isr(p, pv2_now_us());
    }
    wait_submit(&panel, 1); /* Owner does not pump/query/drain to cause submission. */
    assert(pv2_presenter_drain(p, 0) == ESP_ERR_TIMEOUT);
    if (fb) {
        frame(p);    /* Third buffer can queue while first is pending. */
    }
    complete(&panel);
    if (fb) {
        wait_submit(&panel, 2);
        complete(&panel);
    }
    OK(pv2_presenter_drain(p, 1000));
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.completed_frames == (fb ? 2u : 1u));
    pthread_mutex_lock(&panel.lock);
    panel.immediate = true;
    pthread_mutex_unlock(&panel.lock);
    frame(p);
    if (!fb) {
        pv2_presenter_notify_te(p, pv2_now_us());
    }
    wait_submit(&panel, fb ? 3 : 2);
    OK(pv2_presenter_drain(p, 1000));
    destroy(p, &panel);
}

static void bounded_join(void)
{
    panel_t panel;
    init_panel(&panel);
    panel.blocked = true;
    pv2_presenter_t *p = create(&panel, false);
    frame(p);
    pv2_presenter_notify_te(p, pv2_now_us());
    wait_submit(&panel, 1); /* Sender is inside a deliberately blocked driver call. */
    uint64_t start = pv2_now_ms();
    assert(pv2_presenter_stop(p, 5) == ESP_ERR_TIMEOUT);
    assert(pv2_now_ms() - start < 500);
    assert(pv2_presenter_destroy(p) == ESP_ERR_INVALID_STATE);
    pv2_frame_request_t request = {0};
    assert(pv2_presenter_begin_next_frame(p, &request) == ESP_ERR_INVALID_STATE);
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
    pv2_presenter_t *p = create(&panel, false);
    frame(p);
    pv2_presenter_notify_te(p, pv2_now_us());
    wait_submit(&panel, 1);
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    /* A queued copy-progress wake may already have run a no-op service after
     * the rejected submit. last_service_error is a snapshot, not a sticky
     * rejection history. The driver is still rejecting and the frame must
     * remain pending without poisoning the accepted-operation fault. */
    assert(status.last_service_error == ESP_FAIL || status.last_service_error == ESP_OK);
    assert(status.first_error == ESP_OK && status.pending_frames == 1);
    pthread_mutex_lock(&panel.lock);
    panel.reject = false;
    pthread_mutex_unlock(&panel.lock);
    pv2_presenter_notify_te_isr(p, pv2_now_us());
    wait_submit(&panel, 2);
    complete(&panel);
    OK(pv2_presenter_drain(p, 1000));
    destroy(p, &panel);
}

static void wait_te_stop(void)
{
    panel_t panel;
    init_panel(&panel);
    pv2_presenter_t *p = create(&panel, false);
    frame(p); /* No TE: worker must park, and stop must wake it. */
    destroy(p, &panel);
    assert(panel.submissions == 0);
}

static void failure_cleanup(void)
{
    panel_t panel;
    init_panel(&panel);
    pv2_presenter_config_t config = {.background_service = true, .backend_kind = PV2_PRESENTER_TE,
                                     .backend = {.panel = {&panel, attach, submit, quiesce}}
                                    };
    pv2_presenter_t *p;
    assert(pv2_presenter_create(&config, &p) == ESP_ERR_INVALID_ARG && p == NULL);
    assert(panel.callback == NULL);
    pthread_cond_destroy(&panel.condition);
    pthread_mutex_destroy(&panel.lock);
}
int main(void)
{
    autonomous(false);
    autonomous(true);
    bounded_join();
    rejection();
    wait_te_stop();
    failure_cleanup();
    puts("Scheduler: autonomous TE/FB, queued FB, early callback, bounded join and parked exit passed");
    return 0;
}
