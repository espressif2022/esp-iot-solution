/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/backend/te_composer.h"
#include "scan_panel.h"
#include <pthread.h>
#include <stdio.h>
#define OK(x) assert((x)==ESP_OK)
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool block, entered;
    unsigned done;
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
    pthread_mutex_unlock(&g->lock);
    return pv2_pixel_execute_cpu(r);
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
int main(void)
{
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
