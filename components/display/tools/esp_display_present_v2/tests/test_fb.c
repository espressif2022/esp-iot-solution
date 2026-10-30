/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "scan_panel.h"
#include "present_presenter.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#define OK(x) assert((x) == ESP_OK)
static const pv2_backend_ops_t *ops = &pv2_fb_backend;
static pv2_backend_config_t config(scan_panel_t *p, size_t count, pv2_render_contract_t contract)
{
    return (pv2_backend_config_t) {
        .width = 4, .height = 4, .tile_rows = 2,
        .buffer_count = count, .max_frames = 8, .format = PV2_FORMAT_RGB565,
        .render_contract = contract, .panel = scan_interface(p), .allow_active_scanout_writes = count == 1
    };
}

static pv2_frame_t begin(pv2_backend_t *b)
{
    pv2_frame_t frame;
    pv2_frame_request_t request = {0};
    OK(ops->begin(b, &request, &frame));
    return frame;
}

static void write_rect(pv2_backend_t *b, pv2_frame_t f, pv2_rect_t area, uint16_t color)
{
    pv2_backend_caps_t caps;
    OK(ops->get_caps(b, &caps));
    for (uint32_t y = 0; y < area.height;) {
        pv2_lease_t lease;
        OK(ops->acquire(b, f, NULL, 0, &lease));
        uint32_t rows = caps.contract == PV2_CONTRACT_PARTITION && area.height - y > 2 ? 2 : area.height - y;
        size_t stride = caps.contract == PV2_CONTRACT_PARTITION ? area.width * 2 : lease.surface.layout.stride_bytes;
        for (uint32_t row = 0; row < rows; row++) {
            for (uint32_t x = 0; x < area.width; x++) {
                size_t offset = caps.contract == PV2_CONTRACT_PARTITION ? row * stride + x * 2 :
                                (area.y + y + row) * stride + (area.x + x) * 2;
                memcpy((uint8_t *)lease.surface.pixels + offset, &color, 2);
            }
        }
        pv2_region_submit_t sub = {{area.x, area.y + y, area.width, rows}, stride};
        OK(ops->submit_region_ex(b, f, lease.id, &sub));
        y += rows;
    }
}

static pv2_receipt_t full(pv2_backend_t *b, uint16_t color)
{
    pv2_frame_t f = begin(b);
    write_rect(b, f, (pv2_rect_t) {
        0, 0, 4, 4
    }, color);
    pv2_receipt_t receipt;
    OK(ops->end(b, f, &receipt));
    OK(pv2_fb_service(b));
    return receipt;
}

static void finish(pv2_backend_t *b, scan_panel_t *p, pv2_receipt_t r)
{
    scan_confirm(p);
    pv2_frame_status_t status;
    OK(ops->wait(b, r, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status));
    assert(status.present_confirmed && status.terminal && status.result == ESP_OK);
    OK(ops->release_receipt(b, r));
}

static void grid(void)
{
    for (size_t count = 1; count <= 3; count++) {
        for (int mode = PV2_CONTRACT_PARTITION; mode <= PV2_CONTRACT_DIRECT; mode++) {
            scan_panel_t panel = {0};
            pv2_backend_config_t cfg = config(&panel, count, mode);
            pv2_backend_t *b;
            OK(ops->create(&cfg, &b));
            pv2_backend_caps_t caps;
            OK(ops->get_caps(b, &caps));
            assert(caps.writes_active_scanout == (count == 1));
            pv2_receipt_t first = full(b, 0x1234);
            pv2_backend_event_t false_done = {.kind = PV2_EVENT_TRANSFER_DONE, .transaction_cookie = panel.cookie};
            ops->on_event(b, &false_done);
            pv2_frame_status_t status;
            assert(ops->wait(b, first, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
            finish(b, &panel, first);
            void *scanning = panel.current.pixels;
            pv2_frame_t f = begin(b);
            pv2_lease_t lease;
            OK(ops->acquire(b, f, NULL, 0, &lease));
            if (mode != PV2_CONTRACT_PARTITION) {
                assert((lease.surface.pixels == scanning) == (count == 1));
            }
            OK(ops->cancel_region(b, f, lease.id));
            if (mode == PV2_CONTRACT_FULL)
                write_rect(b, f, (pv2_rect_t) {
                0, 0, 4, 4
            }, 0xabcd);
            else
                write_rect(b, f, (pv2_rect_t) {
                1, 1, 2, 2
            }, 0xabcd);
            if (count > 1) {
                uint16_t value;
                memcpy(&value, scanning, 2);
                assert(value == 0x1234); /* Active scan source was never drawn into. */
            }
            pv2_receipt_t second;
            OK(ops->end(b, f, &second));
            OK(pv2_fb_service(b));
            finish(b, &panel, second);
            for (unsigned y = 0; y < 4; y++) {
                for (unsigned x = 0; x < 4; x++) {
                    uint16_t value;
                    memcpy(&value, (const uint8_t *)panel.current.pixels + (y * 4 + x) * 2, 2);
                    uint16_t expected = mode == PV2_CONTRACT_FULL || (x >= 1 && x < 3 && y >= 1 && y < 3) ? 0xabcd : 0x1234;
                    assert(value == expected);
                }
            }
            panel.stop_timeout = true;
            assert(ops->stop(b, 0) == ESP_ERR_TIMEOUT); /* Even a terminal receipt leaves live scanout. */
            assert(ops->destroy(b) == ESP_ERR_INVALID_STATE);
            panel.stop_timeout = false;
            OK(ops->stop(b, 0));
            OK(ops->destroy(b));
        }
    }
}

static void pipeline(void)
{
    scan_panel_t p = {0};
    pv2_backend_config_t cfg = config(&p, 3, PV2_CONTRACT_DIRECT);
    pv2_backend_t *b;
    OK(ops->create(&cfg, &b));
    pv2_receipt_t first = full(b, 0x1111);
    finish(b, &p, first);
    void *old = p.current.pixels;
    pv2_receipt_t second = full(b, 0x2222);
    pv2_frame_t third = begin(b);
    write_rect(b, third, (pv2_rect_t) {
        0, 0, 1, 1
    }, 0x3333);
    pv2_receipt_t receipt;
    OK(ops->end(b, third, &receipt));
    pv2_frame_request_t request = {0};
    pv2_frame_t blocked;
    assert(ops->begin(b, &request, &blocked) == ESP_ERR_NO_MEM);
    assert(p.submissions == 2 && p.current.pixels == old);
    finish(b, &p, second); /* Pump can now submit queued third. */
    assert(p.submissions == 3);
    finish(b, &p, receipt);
    uint16_t value;
    memcpy(&value, p.current.pixels, 2);
    assert(value == 0x3333);
    memcpy(&value, (uint8_t *)p.current.pixels + 2, 2);
    assert(value == 0x2222); /* Repair used latest sealed frame, not stale scanout. */
    third = begin(b);
    write_rect(b, third, (pv2_rect_t) {
        0, 0, 1, 1
    }, 0xeeee);
    OK(ops->cancel_frame(b, third, 0));
    OK(ops->cancel_frame(b, third, 0));
    memcpy(&value, p.current.pixels, 2);
    assert(value == 0x3333);
    OK(ops->stop(b, 0));
    OK(ops->destroy(b));
}

static void errors_and_external(void)
{
    for (unsigned mode = 0; mode < 3; mode++) {
        scan_panel_t p = {.uncertain = mode == 0, .immediate = mode == 1, .early_reject = mode == 1};
        pv2_backend_config_t cfg = config(&p, 2, PV2_CONTRACT_FULL);
        pv2_backend_t *b;
        OK(ops->create(&cfg, &b));
        pv2_receipt_t r = full(b, 7);
        if (mode == 2) {
            pv2_backend_event_t fault_event = {.kind = PV2_EVENT_FAULT, .transaction_cookie = p.cookie, .result = ESP_FAIL};
            ops->on_event(b, &fault_event);
        }
        p.stop_timeout = true;
        assert(ops->stop(b, 0) == ESP_ERR_TIMEOUT);
        p.stop_timeout = false;
        OK(ops->stop(b, 0));
        pv2_frame_status_t status;
        OK(ops->query(b, r, &status));
        assert(status.terminal && status.result != ESP_OK && !status.present_confirmed);
        OK(ops->release_receipt(b, r));
        OK(ops->destroy(b));
    }
    scan_panel_t p = {.reject = true};
    pv2_backend_config_t cfg = config(&p, 2, PV2_CONTRACT_FULL);
    void *external[2] = {aligned_alloc(64, 64), aligned_alloc(64, 64)};
    assert(external[0] && external[1]);
    cfg.external_buffers = external;
    cfg.external_buffer_bytes = 64;
    pv2_backend_t *b;
    OK(ops->create(&cfg, &b));
    pv2_frame_t f = begin(b);
    write_rect(b, f, (pv2_rect_t) {
        0, 0, 4, 4
    }, 8);
    pv2_receipt_t r;
    OK(ops->end(b, f, &r));
    assert(pv2_fb_service(b) == ESP_FAIL);
    p.reject = false;
    OK(pv2_fb_service(b));
    finish(b, &p, r);
    OK(ops->stop(b, 0));
    OK(ops->destroy(b));
    memset(external[0], 0xaa, 64); /* Still owned by caller. */
    free(external[0]);
    free(external[1]);
}

static void named_modes(void)
{
    const pv2_present_mode_t modes[] = {PV2_MODE_NONE, PV2_MODE_DOUBLE_FULL, PV2_MODE_TRIPLE_FULL,
                                        PV2_MODE_DOUBLE_DIRECT, PV2_MODE_DOUBLE_PARTIAL, PV2_MODE_TRIPLE_PARTIAL, PV2_MODE_AUTO
                                       };
    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); i++) {
        scan_panel_t panel = {0};
        pv2_presenter_config_t cfg = {.backend = config(&panel, 3, PV2_CONTRACT_PARTITION),
                                      .backend_kind = PV2_PRESENTER_FB, .mode = modes[i]
                                     };
        cfg.backend.allow_active_scanout_writes = true;
        pv2_presenter_t *p;
        OK(pv2_presenter_create(&cfg, &p));
        pv2_backend_caps_t caps;
        OK(pv2_presenter_get_caps(p, &caps));
        assert(caps.supports_present_confirmation);
        pv2_render_contract_t expected = modes[i] == PV2_MODE_DOUBLE_DIRECT ? PV2_CONTRACT_DIRECT :
                                         (modes[i] == PV2_MODE_DOUBLE_FULL || modes[i] == PV2_MODE_TRIPLE_FULL ? PV2_CONTRACT_FULL : PV2_CONTRACT_PARTITION);
        assert(caps.contract == expected);
        assert(caps.writes_active_scanout == (modes[i] == PV2_MODE_NONE));
        OK(pv2_presenter_stop(p, 0));
        OK(pv2_presenter_destroy(p));
    }
}
typedef struct {
    pv2_backend_t *backend;
    uint64_t cookie;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool start;
} concurrent_event_t;
static void *confirm_thread(void *context)
{
    concurrent_event_t *event = context;
    pthread_mutex_lock(&event->mutex);
    while (!event->start) {
        pthread_cond_wait(&event->condition, &event->mutex);
    }
    pthread_mutex_unlock(&event->mutex);
    pv2_backend_event_t completion = {.kind = PV2_EVENT_SCANOUT_CONFIRMED, .transaction_cookie = event->cookie + 1};
    for (unsigned i = 0; i < 50; i++) {
        (void)ops->on_event_isr(event->backend, &completion);
    }
    completion.transaction_cookie = event->cookie;
    (void)ops->on_event_isr(event->backend, &completion);
    return NULL;
}

static void concurrent_confirmation(void)
{
    scan_panel_t panel = {0};
    pv2_backend_config_t cfg = config(&panel, 2, PV2_CONTRACT_FULL);
    pv2_backend_t *b;
    OK(ops->create(&cfg, &b));
    pv2_receipt_t r = full(b, 0xbeef);
    concurrent_event_t event = {.backend = b, .cookie = panel.cookie};
    assert(pthread_mutex_init(&event.mutex, NULL) == 0);
    assert(pthread_cond_init(&event.condition, NULL) == 0);
    /* Hardware activation/retirement has happened; deliver only its event on
     * another thread. Panel model remains owned by the test's producer task. */
    panel.current = panel.pending;
    panel.pending = (pv2_surface_t) {
        0
    };
    panel.cookie = 0;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, confirm_thread, &event) == 0);
    pthread_mutex_lock(&event.mutex);
    event.start = true;
    pthread_cond_broadcast(&event.condition);
    pthread_mutex_unlock(&event.mutex);
    pv2_frame_status_t status;
    OK(ops->wait(b, r, PV2_COMPLETION_PRESENT_CONFIRMED, 1000, &status));
    assert(status.present_confirmed);
    assert(pthread_join(thread, NULL) == 0);
    OK(ops->release_receipt(b, r));
    OK(ops->stop(b, 0));
    OK(ops->destroy(b));
    pthread_cond_destroy(&event.condition);
    pthread_mutex_destroy(&event.mutex);
}

static void validation(void)
{
    scan_panel_t p = {0};
    pv2_backend_t *b;
    pv2_backend_config_t cfg = config(&p, 1, PV2_CONTRACT_PARTITION);
    cfg.allow_active_scanout_writes = false;
    assert(ops->create(&cfg, &b) == ESP_ERR_INVALID_ARG);
    void *memory = aligned_alloc(64, 128);
    assert(memory);
    void *buffers[2] = {memory, memory};
    cfg = config(&p, 2, PV2_CONTRACT_DIRECT);
    cfg.external_buffers = buffers;
    cfg.external_buffer_bytes = 64;
    assert(ops->create(&cfg, &b) == ESP_ERR_INVALID_ARG);
    buffers[1] = (uint8_t *)memory + 1;
    assert(ops->create(&cfg, &b) == ESP_ERR_INVALID_ARG);
    buffers[1] = (uint8_t *)memory + 64;
    pv2_presenter_config_t pc = {.backend = cfg, .backend_kind = PV2_PRESENTER_FB, .mode = PV2_MODE_TRIPLE_FULL};
    pv2_presenter_t *presenter;
    assert(pv2_presenter_create(&pc, &presenter) == ESP_ERR_INVALID_ARG);
    free(memory);
    cfg = config(&p, 2, PV2_CONTRACT_DIRECT);
    OK(ops->create(&cfg, &b));
    pv2_frame_t f = begin(b);
    write_rect(b, f, (pv2_rect_t) {
        0, 0, 1, 1
    }, 0x1111);
    pv2_frame_coverage_t claimed = {.full_coverage = true};
    assert(ops->end_ex(b, f, &claimed, NULL) == ESP_ERR_INVALID_STATE);
    OK(ops->cancel_frame(b, f, 0));
    OK(ops->stop(b, 0));
    OK(ops->destroy(b));
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

static void asynchronous_partition(unsigned mode)
{
    copy_gate_t g = {.mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER, .fail = mode == 3};
    scan_panel_t panel = {0};
    pv2_backend_config_t cfg = config(&panel, 2, PV2_CONTRACT_PARTITION);
    cfg.tile_rows = 4;
    cfg.async_copy = true;
    cfg.copy_execute = gated_copy;
    cfg.copy_user = &g;
    cfg.on_progress = copied;
    cfg.progress_user = &g;
    pv2_backend_t *b;
    OK(ops->create(&cfg, &b));
    pv2_frame_t f = begin(b);
    pv2_lease_t lease;
    OK(ops->acquire(b, f, NULL, 0, &lease));
    memset(lease.surface.pixels, 0x5a, 32);
    pv2_region_submit_t sub = {.logical_area = {0, 0, 4, mode == 4 ? 2 : 4}, .stride_bytes = 8};
    OK(ops->submit_region_ex(b, f, lease.id, &sub));
    pthread_mutex_lock(&g.mutex);
    while (!g.entered) {
        pthread_cond_wait(&g.changed, &g.mutex);
    }
    pthread_mutex_unlock(&g.mutex);
    assert(ops->acquire(b, f, NULL, 0, &lease) == ESP_ERR_TIMEOUT);
    pv2_receipt_t receipt = {0};
    if (mode == 1) {
        assert(ops->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
        assert(ops->acquire(b, f, NULL, 0, &lease) == ESP_ERR_INVALID_STATE);
    } else {
        OK(ops->end(b, f, mode == 5 ? NULL : &receipt));
        pv2_frame_t next;
        pv2_frame_request_t req = {0};
        assert(ops->begin(b, &req, &next) == ESP_ERR_NO_MEM);
        OK(pv2_fb_service(b));
        assert(panel.submissions == 0);
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
        OK(pv2_fb_service(b));
        if (mode == 1) {
            OK(ops->cancel_frame(b, f, 0));
        }
        if (mode == 0 || mode == 5) {
            assert(panel.cookie);
            scan_confirm(&panel);
            OK(pv2_fb_service(b));
            for (size_t i = 0; i < 32; ++i) {
                assert(panel.pixels[i] == 0x5a);
            }
        } else {
            assert(panel.submissions == 0);
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
    for (unsigned mode = 0; mode < 6; ++mode) {
        asynchronous_partition(mode);
    }
    grid();
    pipeline();
    errors_and_external();
    named_modes();
    concurrent_confirmation();
    validation();
    puts("FB contracts: single/double/triple, scanout retention, queued baseline and faults passed");
    return 0;
}
