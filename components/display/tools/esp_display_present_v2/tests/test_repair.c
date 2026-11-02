/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/backend/te_composer.h"
#include "scan_panel.h"
#include "present_presenter.h"
#include <pthread.h>
#include <stdio.h>
#define OK(x) assert((x)==ESP_OK)
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool block, entered;
    unsigned done;
    esp_err_t result;
} gate_t;
static esp_err_t execute(void *user, const pv2_copy_request_t *r)
{
    gate_t *g = user;
    pthread_mutex_lock(&g->lock);
    g->entered = true;
    pthread_cond_broadcast(&g->changed);
    while (g->block) {
        pthread_cond_wait(&g->changed, &g->lock);
    }
    esp_err_t result = g->result;
    pthread_mutex_unlock(&g->lock);
    return result == ESP_OK ? pv2_pixel_execute_cpu(r) : result;
}

static bool event(void *user, pv2_copy_ticket_t t, const pv2_copy_status_t *status, bool fault)
{
    (void)t;
    (void)status;
    (void)fault;
    gate_t *g = user;
    pthread_mutex_lock(&g->lock);
    g->done++;
    pthread_cond_broadcast(&g->changed);
    pthread_mutex_unlock(&g->lock);
    return false;
}

static bool progress(void *user, bool isr)
{
    assert(!isr);
    event(user, (pv2_copy_ticket_t) {
        0
    }, NULL, false);
    return false;
}

static void wait_enter(gate_t *g)
{
    pthread_mutex_lock(&g->lock);
    while (!g->entered) {
        pthread_cond_wait(&g->changed, &g->lock);
    }
    pthread_mutex_unlock(&g->lock);
}

static void unblock(gate_t *g)
{
    pthread_mutex_lock(&g->lock);
    g->block = false;
    pthread_cond_broadcast(&g->changed);
    pthread_mutex_unlock(&g->lock);
}

static unsigned count(gate_t *g)
{
    pthread_mutex_lock(&g->lock);
    unsigned n = g->done;
    pthread_mutex_unlock(&g->lock);
    return n;
}

static void wait_done(gate_t *g, unsigned n)
{
    pthread_mutex_lock(&g->lock);
    while (g->done == n) {
        pthread_cond_wait(&g->changed, &g->lock);
    }
    pthread_mutex_unlock(&g->lock);
}

static void arm(gate_t *g)
{
    pthread_mutex_lock(&g->lock);
    g->block = true;
    g->entered = false;
    pthread_mutex_unlock(&g->lock);
}

static void te(bool cancel)
{
    gate_t g = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    pv2_te_composer_config_t c = {.width = 4, .height = 4, .format = PV2_FORMAT_RGB565, .pixel = {.max_jobs = 1, .asynchronous = true, .execute = execute, .execute_user = &g, .on_event = event, .user = &g}};
    pv2_te_composer_t *b;
    OK(pv2_te_composer_create(&c, &b));
    pv2_te_frame_t f;
    uint16_t pixels[16];
    for (unsigned i = 0; i < 16; i++) {
        pixels[i] = i + 1;
    }
    pv2_source_t src = {pixels, sizeof(pixels), {4, 4, 8, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}}, out;
    pv2_rect_t full = {0, 0, 4, 4};
    OK(pv2_te_composer_begin(b, &f));
    OK(pv2_te_composer_write(b, f, &src, &full, 0, 0));
    OK(pv2_te_composer_seal(b, f, 1));
    OK(pv2_te_composer_offer(b, f, 1, 1, 10, &out));
    OK(pv2_te_composer_complete(b, f, ESP_OK));
    OK(pv2_te_composer_begin(b, &f));
    arm(&g);
    unsigned n = count(&g);
    assert(pv2_te_composer_seal(b, f, 2) == ESP_ERR_TIMEOUT);
    wait_enter(&g);
    assert(pv2_te_composer_seal(b, f, 2) == ESP_ERR_TIMEOUT);
    assert(pv2_te_composer_offer(b, f, 2, 2, 10, &out) == ESP_ERR_INVALID_STATE);
    if (cancel) {
        assert(pv2_te_composer_cancel(b, f) == ESP_ERR_TIMEOUT);
        assert(pv2_te_composer_stop(b, 0) == ESP_ERR_TIMEOUT);
    }
    unblock(&g);
    wait_done(&g, n);
    if (cancel) {
        OK(pv2_te_composer_stop(b, UINT32_MAX));
        OK(pv2_te_composer_cancel(b, f));
    } else {
        for (;;) {
            n = count(&g);
            esp_err_t e = pv2_te_composer_seal(b, f, 3);
            if (e == ESP_OK) {
                break;
            }
            assert(e == ESP_ERR_TIMEOUT);
            wait_done(&g, n);
        }
        OK(pv2_te_composer_offer(b, f, 3, 3, 10, &out));
        assert(!memcmp(out.pixels, pixels, sizeof(pixels)));
        OK(pv2_te_composer_complete(b, f, ESP_OK));
        OK(pv2_te_composer_stop(b, UINT32_MAX));
    }
    OK(pv2_te_composer_destroy(b));
    pthread_cond_destroy(&g.changed);
    pthread_mutex_destroy(&g.lock);
}

static void fb(bool cancel)
{
    gate_t g = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    scan_panel_t panel = {0};
    const pv2_backend_ops_t *o = &pv2_fb_backend;
    pv2_backend_config_t c = {.width = 4, .height = 4, .buffer_count = 2, .max_frames = 3, .format = PV2_FORMAT_RGB565, .render_contract = PV2_CONTRACT_DIRECT, .panel = scan_interface(&panel), .async_copy = true, .copy_execute = execute, .copy_user = &g, .on_progress = progress, .progress_user = &g};
    pv2_backend_t *b;
    OK(o->create(&c, &b));
    pv2_frame_t f;
    pv2_frame_request_t q = {0};
    pv2_lease_t l;
    pv2_receipt_t r;
    pv2_frame_status_t status;
    OK(o->begin(b, &q, &f));
    OK(o->acquire(b, f, NULL, 0, &l));
    memset(l.surface.pixels, 0x11, 32);
    OK(o->submit_region(b, f, l.id));
    OK(o->end(b, f, &r));
    OK(pv2_fb_service(b));
    scan_confirm(&panel);
    OK(o->wait(b, r, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status));
    OK(o->release_receipt(b, r));
    arm(&g);
    unsigned n = count(&g);
    OK(o->begin(b, &q, &f));
    wait_enter(&g);
    assert(o->acquire(b, f, NULL, 0, &l) == ESP_ERR_TIMEOUT);
    if (cancel) {
        assert(o->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
        assert(o->stop(b, 0) == ESP_ERR_TIMEOUT);
        /* Copy is still blocked; cancel_frame must not falsely succeed after stop(0) sets closing. */
        assert(o->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
        unblock(&g);
        wait_done(&g, n);
        OK(o->cancel_frame(b, f, 0));
        OK(o->stop(b, UINT32_MAX));
        OK(o->destroy(b));
        pthread_cond_destroy(&g.changed);
        pthread_mutex_destroy(&g.lock);
        return;
    }
    unblock(&g);
    wait_done(&g, n);
    OK(o->acquire(b, f, NULL, 0, &l));
    memset(l.surface.pixels, 0xee, 32);
    ((uint16_t*)l.surface.pixels)[0] = 0x2222;
    pv2_region_submit_t sub = {.logical_area = {0, 0, 1, 1}, .stride_bytes = 8};
    OK(o->submit_region_ex(b, f, l.id, &sub));
    arm(&g);
    n = count(&g);
    OK(o->end(b, f, &r));
    wait_enter(&g);
    OK(pv2_fb_service(b));
    assert(panel.submissions == 1);
    if (cancel) {
        assert(o->stop(b, 0) == ESP_ERR_TIMEOUT);
    }
    unblock(&g);
    wait_done(&g, n);
    if (cancel) {
        OK(o->stop(b, UINT32_MAX));
    } else {
        while (!panel.cookie) {
            n = count(&g);
            OK(pv2_fb_service(b));
            if (!panel.cookie) {
                wait_done(&g, n);
            }
        }
        scan_confirm(&panel);
        OK(pv2_fb_service(b));
        assert(((uint16_t*)panel.current.pixels)[0] == 0x2222);
        for (unsigned i = 1; i < 16; i++) {
            assert(((uint16_t*)panel.current.pixels)[i] == 0x1111);
        }
    }
    OK(o->query(b, r, &status));
    assert(status.terminal);
    OK(o->release_receipt(b, r));
    OK(o->stop(b, UINT32_MAX));
    OK(o->destroy(b));
    pthread_cond_destroy(&g.changed);
    pthread_mutex_destroy(&g.lock);
}
/* Sealing a frame cannot release a source that the copy worker still reads.
 * Modes: success, accepted-copy failure, and stop timeout with retained access. */
static void fb_source_completion(unsigned mode)
{
    gate_t gate = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER,
                   .result = mode == 1 ? ESP_FAIL : ESP_OK
                  };
    scan_panel_t panel = {0};
    const pv2_backend_ops_t *ops = &pv2_fb_backend;
    pv2_backend_config_t config = {
        .width = 4, .height = 4, .tile_rows = 4, .buffer_count = 2, .max_frames = 3,
        .format = PV2_FORMAT_RGB565, .render_contract = PV2_CONTRACT_PARTITION,
        .panel = scan_interface(&panel), .async_copy = true,
        .copy_execute = execute, .copy_user = &gate, .on_progress = progress, .progress_user = &gate,
    };
    pv2_backend_t *backend;
    OK(ops->create(&config, &backend));
    pv2_frame_t frame;
    pv2_frame_request_t request = {.full_redraw = true};
    OK(ops->begin(backend, &request, &frame));
    pv2_lease_t lease;
    OK(ops->acquire(backend, frame, NULL, 0, &lease));
    memset(lease.surface.pixels, 0x5a, 32);
    arm(&gate);
    unsigned completed = count(&gate);
    OK(ops->submit_region(backend, frame, lease.id));
    wait_enter(&gate);
    pv2_receipt_t receipt;
    OK(ops->end(backend, frame, &receipt));
    pv2_frame_status_t status;
    OK(ops->query(backend, receipt, &status));
    assert(status.sealed && !status.source_released && !status.terminal && !status.present_confirmed);
    assert(ops->wait(backend, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(ops->wait(backend, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(ops->release_receipt(backend, receipt) == ESP_ERR_INVALID_STATE);
    assert(panel.submissions == 0);
    if (mode == 2) {
        assert(ops->stop(backend, 0) == ESP_ERR_TIMEOUT);
        OK(ops->query(backend, receipt, &status));
        assert(!status.source_released && !status.terminal);
    }
    unblock(&gate);
    wait_done(&gate, completed);
    if (mode == 2) {
        OK(ops->stop(backend, UINT32_MAX));
    } else {
        OK(pv2_fb_service(backend));
        if (mode == 0) {
            assert(panel.submissions == 1);
            OK(ops->query(backend, receipt, &status));
            assert(!status.source_released && !status.terminal);
            assert(ops->wait(backend, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
            scan_confirm(&panel);
        } else {
            assert(panel.submissions == 0);
        }
    }
    OK(ops->wait(backend, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status));
    assert(status.terminal && status.source_released);
    assert(status.present_confirmed == (mode == 0));
    assert(status.result == (mode == 0 ? ESP_OK : mode == 1 ? ESP_FAIL : ESP_ERR_INVALID_STATE));
    if (mode != 0) {
        /* Source drain does not clear the accepted-operation sticky fault. */
        pv2_backend_fault_t fault;
        OK(ops->get_fault(backend, &fault));
        assert(fault.result == status.result);
        assert(ops->begin(backend, &request, &frame) == ESP_ERR_INVALID_STATE);
    }
    OK(ops->release_receipt(backend, receipt));
    OK(ops->stop(backend, UINT32_MAX));
    OK(ops->destroy(backend));
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.lock);
}

static void presenter_source_completion(void)
{
    gate_t gate = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    scan_panel_t panel = {0};
    pv2_presenter_config_t config = {
        .backend_kind = PV2_PRESENTER_FB, .mode = PV2_MODE_DOUBLE_PARTIAL,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 4, .buffer_count = 2, .max_frames = 3,
            .format = PV2_FORMAT_RGB565, .panel = scan_interface(&panel), .async_copy = true,
            .copy_execute = execute, .copy_user = &gate, .on_progress = progress, .progress_user = &gate,
        },
    };
    pv2_presenter_t *presenter;
    OK(pv2_presenter_create(&config, &presenter));
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0x5a, 32);
    arm(&gate);
    unsigned completed = count(&gate);
    pv2_area_t area = {0, 0, 3, 3};
    OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 8));
    wait_enter(&gate);
    pv2_presenter_receipt_t receipt;
    OK(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt));
    pv2_frame_status_t status;
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(!status.source_released && !status.terminal && !status.present_confirmed);
    assert(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_release_receipt(presenter, receipt) == ESP_ERR_INVALID_STATE);
    unblock(&gate);
    wait_done(&gate, completed);
    OK(pv2_presenter_service(presenter));
    assert(panel.cookie);
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(!status.source_released && !status.terminal);
    scan_confirm(&panel);
    OK(pv2_presenter_service(presenter));
    OK(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status));
    assert(status.source_released && status.terminal && status.present_confirmed);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    OK(pv2_presenter_stop(presenter, UINT32_MAX));
    OK(pv2_presenter_destroy(presenter));
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.lock);
}

int main(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        fb_source_completion(mode);
    }
    presenter_source_completion();
    te(false);
    te(true);
    fb(false);
    fb(true);
    /* Also cover stop(0) during post-seal async repair. */
    {
        gate_t g = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
        scan_panel_t panel = {0};
        const pv2_backend_ops_t *o = &pv2_fb_backend;
        pv2_backend_config_t c = {.width = 4, .height = 4, .buffer_count = 2, .max_frames = 3, .format = PV2_FORMAT_RGB565, .render_contract = PV2_CONTRACT_DIRECT, .panel = scan_interface(&panel), .async_copy = true, .copy_execute = execute, .copy_user = &g, .on_progress = progress, .progress_user = &g};
        pv2_backend_t *b;
        OK(o->create(&c, &b));
        pv2_frame_t f;
        pv2_frame_request_t q = {0};
        pv2_lease_t l;
        pv2_receipt_t r;
        pv2_frame_status_t status;
        OK(o->begin(b, &q, &f));
        OK(o->acquire(b, f, NULL, 0, &l));
        memset(l.surface.pixels, 0x11, 32);
        OK(o->submit_region(b, f, l.id));
        OK(o->end(b, f, &r));
        OK(pv2_fb_service(b));
        scan_confirm(&panel);
        OK(o->wait(b, r, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status));
        OK(o->release_receipt(b, r));
        arm(&g);
        unsigned n = count(&g);
        OK(o->begin(b, &q, &f));
        wait_enter(&g);
        unblock(&g);
        wait_done(&g, n);
        OK(o->acquire(b, f, NULL, 0, &l));
        pv2_region_submit_t sub = {.logical_area = {0, 0, 1, 1}, .stride_bytes = 8};
        OK(o->submit_region_ex(b, f, l.id, &sub));
        arm(&g);
        n = count(&g);
        OK(o->end(b, f, &r));
        wait_enter(&g);
        assert(o->stop(b, 0) == ESP_ERR_TIMEOUT);
        unblock(&g);
        wait_done(&g, n);
        OK(o->stop(b, UINT32_MAX));
        OK(o->query(b, r, &status));
        assert(status.terminal);
        OK(o->release_receipt(b, r));
        OK(o->destroy(b));
        pthread_cond_destroy(&g.changed);
        pthread_mutex_destroy(&g.lock);
    }
    puts("repair: gated TE spans, FB prefill/DIRECT, cancel and stop retention passed");
}
