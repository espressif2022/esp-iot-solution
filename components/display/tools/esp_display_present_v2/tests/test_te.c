/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "test_panel.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>

#define OK(call) assert((call) == ESP_OK)
static const pv2_backend_ops_t *ops = &pv2_te_backend;

typedef struct {
    test_panel_t panel;
    bool uncertain;
    bool early_reject;
    bool abort_on_stop;
} fixture_t;
static pv2_tx_result_t submit(void *context, const pv2_surface_t *surface,
                              const pv2_rect_t *area, uint64_t cookie)
{
    fixture_t *fixture = context;
    pv2_tx_result_t result = test_panel_submit(&fixture->panel, surface, area, cookie);
    if (fixture->uncertain) {
        result = (pv2_tx_result_t) {
            PV2_TX_UNCERTAIN, ESP_FAIL
        };
    }
    if (fixture->early_reject) {
        result = (pv2_tx_result_t) {
            PV2_TX_REJECTED, ESP_FAIL
        };
    }
    return result;
}

static esp_err_t stop(void *context, uint32_t timeout)
{
    fixture_t *fixture = context;
    if (fixture->abort_on_stop) {
        fixture->panel.count = 0; /* Simulated safe hardware abort, no callback. */
    }
    return test_panel_stop(&fixture->panel, timeout);
}

static uint64_t now_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static void edge(pv2_backend_t *backend)
{
    static _Thread_local uint64_t previous;
    uint64_t timestamp;
    do {
        timestamp = now_us();
    } while (timestamp <= previous);
    previous = timestamp;
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp};
    (void)ops->on_event_isr(backend, &event);
}

static pv2_backend_t *create(fixture_t *fixture)
{
    fixture->panel.width = 4;
    fixture->panel.height = 4;
    fixture->panel.bpp = 2;
    pv2_backend_config_t config = {
        .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
        .format = PV2_FORMAT_RGB565, .draw_order = PV2_ORDER_NATIVE, .panel_order = PV2_ORDER_NATIVE,
        .panel = {fixture, test_panel_attach, submit, stop},
        .te_window_us = 1000000, .te_transfer_budget_us = 1000,
    };
    pv2_backend_t *backend;
    OK(ops->create(&config, &backend));
    return backend;
}

static pv2_frame_t begin(pv2_backend_t *backend)
{
    pv2_frame_t frame;
    pv2_frame_request_t request = {.full_redraw = true};
    OK(ops->begin(backend, &request, &frame));
    return frame;
}

static void write_area(pv2_backend_t *backend, pv2_frame_t frame, pv2_rect_t area, uint16_t color)
{
    pv2_lease_t lease;
    OK(ops->acquire(backend, frame, NULL, 0, &lease));
    size_t stride = area.width * 2 + 2; /* Padded rows into a narrow destination. */
    if (area.width == 4) {
        stride = 8;
    }
    for (uint32_t y = 0; y < area.height; y++) {
        for (uint32_t x = 0; x < area.width; x++) {
            memcpy((uint8_t *)lease.surface.pixels + y * stride + x * 2, &color, 2);
        }
    }
    pv2_region_submit_t submission = {area, stride};
    OK(ops->submit_region_ex(backend, frame, lease.id, &submission));
    assert(ops->submit_region(backend, frame, lease.id) == ESP_ERR_INVALID_STATE);
}

static pv2_receipt_t full(pv2_backend_t *backend, bool receipt)
{
    pv2_frame_t frame = begin(backend);
    write_area(backend, frame, (pv2_rect_t) {
        0, 0, 4, 2
    }, 0x1234);
    write_area(backend, frame, (pv2_rect_t) {
        0, 2, 4, 2
    }, 0x1234);
    pv2_receipt_t result = {0};
    OK(ops->end(backend, frame, receipt ? &result : NULL));
    return result;
}

static void destroy(pv2_backend_t *backend)
{
    OK(ops->stop(backend, 0));
    OK(ops->destroy(backend));
}

static void lifecycle(void)
{
    fixture_t fixture = {0};
    pv2_backend_t *backend = create(&fixture);
    pv2_frame_t frame = begin(backend);
    pv2_frame_coverage_t coverage = {.full_coverage = true};
    assert(ops->end_ex(backend, frame, &coverage, NULL) == ESP_ERR_INVALID_STATE);
    OK(ops->cancel_frame(backend, frame, 0));
    OK(ops->cancel_frame(backend, frame, 0));
    pv2_receipt_t receipt = full(backend, true);
    edge(backend); /* ISR never directly sends. */
    assert(fixture.panel.submitted == 0);
    OK(pv2_te_service(backend));
    assert(fixture.panel.count == 1);
    pv2_frame_status_t status;
    assert(ops->wait(backend, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(!status.terminal);
    assert(ops->release_receipt(backend, receipt) == ESP_ERR_INVALID_STATE);
    pv2_frame_request_t request = {0};
    assert(ops->begin(backend, &request, &frame) == ESP_ERR_NO_MEM);
    test_panel_complete(&fixture.panel);
    OK(ops->query(backend, receipt, &status));
    assert(status.terminal && status.result == ESP_OK && !status.present_confirmed);
    OK(ops->release_receipt(backend, receipt));
    frame = begin(backend);
    write_area(backend, frame, (pv2_rect_t) {
        1, 1, 2, 2
    }, 0xabcd);
    OK(ops->end(backend, frame, NULL));
    OK(pv2_te_service(backend)); /* Prior edge cannot release new frame. */
    assert(fixture.panel.count == 0);
    fixture.panel.reject = true;
    edge(backend);
    assert(pv2_te_service(backend) == ESP_FAIL);
    fixture.panel.reject = false;
    OK(pv2_te_service(backend));
    assert(fixture.panel.count == 0); /* rejection consumes edge */
    edge(backend);
    OK(pv2_te_service(backend));
    assert(fixture.panel.count == 1);
    test_panel_complete(&fixture.panel);
    OK(pv2_te_service(backend));
    for (unsigned y = 0; y < 4; y++) {
        for (unsigned x = 0; x < 4; x++) {
            uint16_t value;
            memcpy(&value, fixture.panel.pixels + (y * 4 + x) * 2, 2);
            assert(value == (x >= 1 && x <= 2 && y >= 1 && y <= 2 ? 0xabcd : 0x1234));
        }
    }
    /* Fire-and-forget records really recycle; early completions need no future ISR. */
    fixture.panel.immediate = true;
    for (unsigned i = 0; i < 20; i++) {
        (void)full(backend, false);
        edge(backend);
        OK(pv2_te_service(backend));
    }
    destroy(backend);
}

static void channel_fault(void)
{
    fixture_t fixture = {0};
    pv2_backend_t *backend = create(&fixture);
    pv2_receipt_t receipt = full(backend, true);
    pv2_backend_event_t fault = {.kind = PV2_EVENT_FAULT, .result = ESP_FAIL};
    ops->on_event(backend, &fault);
    edge(backend);
    pv2_frame_status_t status;
    OK(ops->query(backend, receipt, &status));
    assert(status.terminal && status.result == ESP_FAIL && fixture.panel.submitted == 0);
    pv2_frame_request_t request = {0};
    pv2_frame_t frame;
    assert(ops->begin(backend, &request, &frame) == ESP_ERR_INVALID_STATE);
    OK(ops->release_receipt(backend, receipt));
    destroy(backend);
}

static void failures(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        fixture_t fixture = {.uncertain = mode == 0, .early_reject = mode == 1,
                             .abort_on_stop = mode == 2
                            };
        fixture.panel.immediate = mode == 1;
        pv2_backend_t *backend = create(&fixture);
        pv2_receipt_t receipt = full(backend, true);
        edge(backend);
        OK(pv2_te_service(backend));
        if (mode == 3) {
            pv2_backend_event_t fault = {.kind = PV2_EVENT_FAULT, .result = ESP_FAIL,
                                         .transaction_cookie = fixture.panel.pending[0].cookie
                                        };
            ops->on_event(backend, &fault);
        }
        if (mode != 1 && mode != 2) {
            assert(ops->stop(backend, 0) == ESP_ERR_TIMEOUT);
            assert(ops->destroy(backend) == ESP_ERR_INVALID_STATE);
            test_panel_complete(&fixture.panel);
        }
        OK(ops->stop(backend, 0));
        pv2_frame_status_t status;
        OK(ops->query(backend, receipt, &status));
        assert(status.terminal && status.result != ESP_OK);
        assert(ops->destroy(backend) == ESP_ERR_INVALID_STATE);
        pv2_backend_fault_t fault;
        OK(ops->get_fault(backend, &fault));
        assert(fault.result != ESP_OK && fault.frame.value == receipt.value);
        OK(ops->release_receipt(backend, receipt));
        OK(ops->destroy(backend));
    }
}

static void ready_stop_and_receipts(void)
{
    fixture_t fixture = {0};
    pv2_backend_t *backend = create(&fixture);
    pv2_receipt_t receipt = full(backend, true);
    edge(backend);
    OK(ops->stop(backend, 0)); /* Must not send READY while stopping. */
    assert(fixture.panel.submitted == 0);
    pv2_frame_status_t status;
    OK(ops->query(backend, receipt, &status));
    assert(status.terminal && status.result != ESP_OK);
    OK(ops->release_receipt(backend, receipt));
    OK(ops->destroy(backend));
    fixture = (fixture_t) {
        0
    };
    fixture.panel.immediate = true;
    backend = create(&fixture);
    pv2_receipt_t receipts[2];
    for (unsigned i = 0; i < 2; i++) {
        receipts[i] = full(backend, true);
        edge(backend);
        OK(pv2_te_service(backend));
    }
    pv2_frame_t frame;
    pv2_frame_request_t request = {0};
    assert(ops->begin(backend, &request, &frame) == ESP_ERR_NO_MEM);
    OK(ops->release_receipt(backend, receipts[0]));
    frame = begin(backend);
    OK(ops->cancel_frame(backend, frame, 0));
    OK(ops->release_receipt(backend, receipts[1]));
    destroy(backend);
}
/* Callback thread races mailbox recording with task queries/stop. Test panel
 * fields themselves stay task-owned; event injection uses the public ISR API. */
typedef struct {
    pv2_backend_t *backend;
    uint64_t cookie;
} injection_t;
static void *inject(void *arg)
{
    injection_t *injection = arg;
    for (unsigned i = 0; i < 300; i++) {
        edge(injection->backend);
    }
    pv2_backend_event_t done = {.kind = PV2_EVENT_TRANSFER_DONE, .transaction_cookie = injection->cookie};
    (void)ops->on_event_isr(injection->backend, &done);
    return NULL;
}

static void threaded_events(void)
{
    for (unsigned i = 0; i < 30; i++) {
        fixture_t fixture = {0};
        pv2_backend_t *backend = create(&fixture);
        pv2_receipt_t receipt = full(backend, true);
        edge(backend);
        OK(pv2_te_service(backend));
        assert(fixture.panel.count == 1);
        injection_t injection = {backend, fixture.panel.pending[0].cookie};
        /* Simulated DMA reads have ended before completion is sent. */
        fixture.panel.count = 0;
        pthread_t thread;
        assert(pthread_create(&thread, NULL, inject, &injection) == 0);
        pv2_frame_status_t status;
        for (unsigned j = 0; j < 20; j++) {
            OK(ops->query(backend, receipt, &status));
        }
        OK(ops->stop(backend, 0));
        assert(pthread_join(thread, NULL) == 0); /* external TE producer exit barrier */
        OK(ops->query(backend, receipt, &status));
        assert(status.terminal);
        OK(ops->release_receipt(backend, receipt));
        OK(ops->destroy(backend));
    }
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool entered, release;
    size_t completed;
    bool fail;
} copy_gate_t;
static esp_err_t gated_copy(void *arg, const pv2_copy_request_t *r)
{
    copy_gate_t *g = arg;
    pthread_mutex_lock(&g->mutex);
    g->entered = true;
    pthread_cond_broadcast(&g->changed);
    while (!g->release) {
        pthread_cond_wait(&g->changed, &g->mutex);
    }
    pthread_mutex_unlock(&g->mutex);
    if (g->fail) {
        return ESP_FAIL;
    }
    for (uint32_t y = 0; y < r->source_area.height; ++y) {
        memcpy((uint8_t *)r->destination.pixels + (r->destination_y + y) * r->destination.layout.stride_bytes + r->destination_x * 2,
               (const uint8_t *)r->source.pixels + (r->source_area.y + y) * r->source.layout.stride_bytes + r->source_area.x * 2,
               r->source_area.width * 2);
    }
    return ESP_OK;
}

static bool copied(void *arg, bool isr)
{
    assert(!isr);
    copy_gate_t *g = arg;
    pthread_mutex_lock(&g->mutex);
    g->completed++;
    pthread_cond_broadcast(&g->changed);
    pthread_mutex_unlock(&g->mutex);
    return false;
}

static void asynchronous_copy(unsigned mode)
{
    copy_gate_t g = {.mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER, .fail = mode == 3};
    test_panel_t panel = {.width = 2, .height = 2, .bpp = 2};
    pv2_backend_config_t cfg = {.width = 2, .height = 2, .tile_rows = 2, .buffer_count = 1, .max_frames = 2,
                                .format = PV2_FORMAT_RGB565, .panel = test_panel_interface(&panel), .te_window_us = 1000000,
                                .te_transfer_budget_us = 1, .async_copy = true, .copy_execute = gated_copy, .copy_user = &g,
                                .on_progress = copied, .progress_user = &g
                               };
    pv2_backend_t *b;
    OK(ops->create(&cfg, &b));
    pv2_frame_t f = begin(b);
    pv2_lease_t lease;
    OK(ops->acquire(b, f, NULL, 0, &lease));
    memset(lease.surface.pixels, 0x5a, 8);
    OK(ops->submit_region(b, f, lease.id));
    pthread_mutex_lock(&g.mutex);
    while (!g.entered) {
        pthread_cond_wait(&g.changed, &g.mutex);
    }
    pthread_mutex_unlock(&g.mutex);
    assert(ops->acquire(b, f, NULL, 0, &lease) == ESP_ERR_TIMEOUT);
    pv2_receipt_t receipt = {0};
    if (mode == 1) {
        assert(ops->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
    } else {
        OK(ops->end(b, f, &receipt));
        edge(b);
        OK(pv2_te_service(b));
        assert(panel.submitted == 0); /* Never sends a partially copied frame. */
        if (mode == 2) {
            assert(ops->stop(b, 0) == ESP_ERR_TIMEOUT);
            assert(ops->destroy(b) == ESP_ERR_INVALID_STATE);
        }
    }
    pthread_mutex_lock(&g.mutex);
    g.release = true;
    pthread_cond_broadcast(&g.changed);
    while (!g.completed) {
        pthread_cond_wait(&g.changed, &g.mutex);
    }
    pthread_mutex_unlock(&g.mutex);
    if (mode == 2) {
        OK(ops->stop(b, UINT32_MAX));
    } else {
        OK(pv2_te_service(b));
        assert(panel.submitted == 0); /* Old edge predates COPY_DRAIN -> READY. */
        if (mode == 1) {
            OK(ops->cancel_frame(b, f, 0));
        }
        if (mode == 0) {
            edge(b);
            OK(pv2_te_service(b));
            assert(panel.count == 1);
            test_panel_complete(&panel);
            for (size_t i = 0; i < 8; ++i) {
                assert(panel.pixels[i] == 0x5a);
            }
        }
    }
    if (receipt.value) {
        pv2_frame_status_t status;
        OK(ops->query(b, receipt, &status));
        assert(status.terminal);
        assert(status.result == (mode == 0 ? ESP_OK : mode == 3 ? ESP_FAIL : ESP_ERR_INVALID_STATE));
        OK(ops->release_receipt(b, receipt));
    }
    OK(ops->stop(b, UINT32_MAX));
    OK(ops->destroy(b));
    pthread_cond_destroy(&g.changed);
    pthread_mutex_destroy(&g.mutex);
}
int main(void)
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        asynchronous_copy(mode);
    }
    lifecycle();
    failures();
    channel_fault();
    ready_stop_and_receipts();
    threaded_events();
    puts("TE backend lifecycle, timing gate and callback races passed");
    return 0;
}
