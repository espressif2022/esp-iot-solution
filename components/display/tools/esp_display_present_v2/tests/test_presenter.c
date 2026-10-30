/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_presenter.h"
#include "test_panel.h"
#include "scan_panel.h"
#include <pthread.h>
#include <stdio.h>

static pv2_presenter_t *make(test_panel_t *panel, size_t frames)
{
    pv2_presenter_config_t config = { .backend = {
            .width = panel->width, .height = panel->height, .tile_rows = 2,
            .buffer_count = 2, .max_frames = frames, .format = PV2_FORMAT_RGB565,
            .panel = test_panel_interface(panel)
        }
    };
    pv2_presenter_t *p;
    assert(pv2_presenter_create(&config, &p) == ESP_OK);
    return p;
}

static void begin(pv2_presenter_t *p)
{
    const pv2_frame_request_t request = {0};
    assert(pv2_presenter_begin_next_frame(p, &request) == ESP_OK);
}

static void send(pv2_presenter_t *p)
{
    pv2_lease_t lease;
    assert(pv2_presenter_acquire_buffer(p, &lease) == ESP_OK);
    memset(lease.surface.pixels, 0x5A, lease.surface.capacity_bytes);
    pv2_area_t area = {0, 0, 3, 1};
    assert(pv2_presenter_submit_buffer(p, &lease, &area, 8) == ESP_OK);
}

static void *foreign_task(void *user)
{
    pv2_presenter_status_t status;
    assert(pv2_presenter_get_status(user, &status) == ESP_ERR_INVALID_STATE);
    assert(pv2_presenter_stop(user, 0) == ESP_ERR_INVALID_STATE);
    return NULL;
}

static void leases_and_ownership(void)
{
    test_panel_t a = {.width = 4, .height = 4, .bpp = 2, .immediate = true};
    test_panel_t b = {.width = 4, .height = 4, .bpp = 2, .immediate = true};
    pv2_presenter_t *p = make(&a, 2), *other = make(&b, 2);
    pv2_lease_t lease, foreign, unused;
    assert(pv2_presenter_acquire_buffer(p, &lease) == ESP_ERR_INVALID_STATE);
    begin(p);
    begin(other);
    assert(pv2_presenter_acquire_buffer(p, &lease) == ESP_OK);
    assert(pv2_presenter_acquire_buffer(other, &foreign) == ESP_OK);
    assert(pv2_presenter_acquire_buffer(p, &unused) == ESP_ERR_INVALID_STATE);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_ERR_INVALID_STATE);
    uint32_t rows;
    assert(pv2_presenter_resolve_rows(p, &lease, 8, 9, &rows) == ESP_OK && rows == 2);
    assert(pv2_presenter_resolve_rows(p, &lease, 7, 2, &rows) == ESP_ERR_INVALID_SIZE);
    pv2_area_t area = {1, 1, 2, 2};
    assert(pv2_presenter_submit_buffer(p, &foreign, &area, 4) == ESP_ERR_INVALID_STATE);
    pv2_area_t bad = {-1, 0, 2, 2};
    assert(pv2_presenter_submit_buffer(p, &lease, &bad, 4) == ESP_ERR_INVALID_ARG);
    memset(lease.surface.pixels, 0xA3, 8);
    a.reject = true;
    assert(pv2_presenter_submit_buffer(p, &lease, &area, 4) == ESP_FAIL);
    a.reject = false;
    assert(pv2_presenter_submit_buffer(p, &lease, &area, 4) == ESP_OK);
    assert(pv2_presenter_submit_buffer(p, &lease, &area, 4) == ESP_ERR_INVALID_STATE);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    assert(pv2_presenter_drain(p, 0) == ESP_OK);
    assert(a.pixels[(1 * 4 + 1) * 2] == 0xA3 && a.pixels[0] == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, foreign_task, p) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pv2_presenter_cancel_frame(other) == ESP_OK);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    assert(pv2_presenter_stop(other, 0) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_OK);
    assert(pv2_presenter_destroy(other) == ESP_OK);
}

static void receipts_cancel_and_stop(void)
{
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    pv2_presenter_t *p = make(&panel, 1);
    begin(p);
    send(p);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    const pv2_frame_request_t request = {0};
    assert(pv2_presenter_begin_next_frame(p, &request) == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_drain(p, 0) == ESP_ERR_TIMEOUT);
    test_panel_complete(&panel);
    assert(pv2_presenter_drain(p, 0) == ESP_OK);
    /* drain is not stop: the next frame is still accepted. */
    begin(p);
    send(p);
    assert(pv2_presenter_cancel_frame(p) == ESP_ERR_TIMEOUT);
    test_panel_complete(&panel);
    assert(pv2_presenter_cancel_frame(p) == ESP_OK);
    assert(pv2_presenter_cancel_frame(p) == ESP_OK);
    begin(p);
    send(p);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    assert(pv2_presenter_stop(p, 0) == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_destroy(p) == ESP_ERR_INVALID_STATE);
    test_panel_complete(&panel);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    pv2_presenter_status_t status;
    assert(pv2_presenter_get_status(p, &status) == ESP_OK);
    assert(status.state == PV2_PRESENTER_STOPPED && status.pending_frames == 0 &&
           status.committed_frames == 2 && status.completed_frames == 2);
    assert(pv2_presenter_destroy(p) == ESP_OK);
}

static void async_error_and_recycle(void)
{
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2, .immediate = true};
    pv2_presenter_t *p = make(&panel, 1);
    for (int i = 0; i < 20; ++i) {
        begin(p);
        send(p);
        assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    }
    panel.immediate = false;
    begin(p);
    send(p);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    pv2_backend_event_t event = {.kind = PV2_EVENT_FAULT,
                                 .transaction_cookie = panel.pending[0].cookie, .result = ESP_FAIL
                                };
    panel.callback(panel.user, &event, false);
    assert(pv2_presenter_drain(p, 0) == ESP_ERR_TIMEOUT);
    test_panel_complete(&panel);
    assert(pv2_presenter_drain(p, 0) == ESP_FAIL);
    pv2_frame_request_t request = {0};
    assert(pv2_presenter_begin_next_frame(p, &request) == ESP_FAIL);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_OK);
}

typedef struct {
    pv2_presenter_t *p;
    bool reject;
} handoff_t;
static void *new_producer(void *user)
{
    handoff_t *h = user;
    assert(pv2_presenter_rebind_producer(h->p, 1000) == (h->reject ? ESP_ERR_INVALID_STATE : ESP_OK));
    if (!h->reject) {
        pv2_presenter_status_t status;
        assert(pv2_presenter_get_status(h->p, &status) == ESP_OK);
        assert(status.pending_frames == 1); /* Sealed transfer survives handoff. */
    }
    return NULL;
}

static void handoff_and_policy(void)
{
    pv2_presenter_config_t cfg = {.backend_kind = PV2_PRESENTER_FB, .mode = PV2_MODE_TRIPLE_FULL};
    size_t count;
    assert(pv2_presenter_get_required_frame_buffer_count(&cfg, &count) == ESP_OK && count == 3);
    cfg.backend_kind = PV2_PRESENTER_GRAM;
    assert(pv2_presenter_get_required_frame_buffer_count(&cfg, &count) == ESP_ERR_NOT_SUPPORTED && count == 0);
    cfg.mode = PV2_MODE_TE_SYNC;
    assert(pv2_presenter_get_required_frame_buffer_count(&cfg, &count) == ESP_OK && count == 0);
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    pv2_presenter_t *p = make(&panel, 2);
    void *buffers[2];
    size_t bytes;
    assert(pv2_presenter_get_draw_buffers(p, buffers, 2, &count, &bytes) == ESP_OK);
    assert(count == 2 && bytes == 16 && buffers[0] != buffers[1]);
    begin(p);
    send(p);
    pthread_t thread;
    handoff_t handoff = {p, true};
    assert(pthread_create(&thread, NULL, new_producer, &handoff) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    handoff.reject = false;
    assert(pthread_create(&thread, NULL, new_producer, &handoff) == 0);
    assert(pthread_join(thread, NULL) == 0);
    pv2_presenter_status_t status;
    assert(pv2_presenter_get_status(p, &status) == ESP_ERR_INVALID_STATE);
    assert(pv2_presenter_rebind_producer(p, 0) == ESP_OK);
    test_panel_complete(&panel);
    assert(pv2_presenter_drain(p, 0) == ESP_OK);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_OK);
}

static void explicit_receipts(void)
{
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    test_panel_t other_panel = {.width = 4, .height = 4, .bpp = 2};
    pv2_presenter_t *p = make(&panel, 2), *other = make(&other_panel, 2);
    pv2_presenter_receipt_t receipt;
    begin(p);
    send(p);
    assert(pv2_presenter_commit_with_receipt(p, NULL, &receipt) == ESP_OK);
    pv2_frame_status_t status;
    assert(pv2_presenter_query_frame(other, receipt, &status) == ESP_ERR_INVALID_STATE);
    assert(pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status) == ESP_ERR_NOT_SUPPORTED);
    assert(pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_release_receipt(p, receipt) == ESP_ERR_INVALID_STATE);
    test_panel_complete(&panel);
    assert(pv2_presenter_drain(p, 0) == ESP_OK);
    assert(pv2_presenter_query_frame(p, receipt, &status) == ESP_OK && status.terminal && status.source_released && !status.present_confirmed);
    assert(pv2_presenter_release_receipt(p, receipt) == ESP_OK);
    assert(pv2_presenter_query_frame(p, receipt, &status) == ESP_ERR_INVALID_STATE);
    begin(p);
    send(p);
    assert(pv2_presenter_commit_with_receipt(p, NULL, &receipt) == ESP_OK);
    assert(pv2_presenter_cancel_tracking(p, receipt) == ESP_OK);
    assert(panel.count == 1); /* Observation cancellation never ends DMA. */
    assert(pv2_presenter_stop(p, 0) == ESP_ERR_TIMEOUT);
    test_panel_complete(&panel);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_OK);
    assert(pv2_presenter_stop(other, 0) == ESP_OK);
    assert(pv2_presenter_destroy(other) == ESP_OK);

    panel.immediate = true;
    p = make(&panel, 2);
    pv2_presenter_receipt_t receipts[32];
    for (size_t i = 0; i < 32; ++i) {
        begin(p);
        send(p);
        assert(pv2_presenter_commit_with_receipt(p, NULL, &receipts[i]) == ESP_OK);
    }
    begin(p);
    send(p);
    assert(pv2_presenter_commit_with_receipt(p, NULL, &receipt) == ESP_ERR_NO_MEM && !receipt.value);
    /* Exhausted observers do not discard the still-open frame. */
    assert(pv2_presenter_commit_frame(p, NULL) == ESP_OK);
    assert(pv2_presenter_stop(p, 0) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_ERR_INVALID_STATE);
    for (size_t i = 0; i < 32; ++i) {
        assert(pv2_presenter_release_receipt(p, receipts[i]) == ESP_OK);
    }
    assert(pv2_presenter_destroy(p) == ESP_OK);
}

static void rotation_and_confirmation(void)
{
    scan_panel_t panel = {0};
    pv2_presenter_config_t config = {.backend_kind = PV2_PRESENTER_FB, .mode = PV2_MODE_DOUBLE_FULL,
                                     .backend = {.width = 4, .height = 2, .tile_rows = 2, .buffer_count = 2, .max_frames = 2, .format = PV2_FORMAT_RGB565, .panel = scan_interface(&panel)}
                                    };
    pv2_presenter_t *p;
    assert(pv2_presenter_create(&config, &p) == ESP_OK);
    begin(p);
    send(p);
    pv2_presenter_receipt_t receipt;
    assert(pv2_presenter_commit_with_receipt(p, NULL, &receipt) == ESP_OK);
    assert(pv2_presenter_set_rotation(p, PV2_ROTATE_90, 0) == ESP_ERR_TIMEOUT);
    pv2_backend_caps_t caps;
    assert(pv2_presenter_get_caps(p, &caps) == ESP_OK && caps.draw_layout.width == 4);
    pv2_frame_status_t status;
    assert(pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status) == ESP_ERR_TIMEOUT);
    scan_confirm(&panel);
    assert(pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 0, &status) == ESP_OK && status.present_confirmed);
    assert(pv2_presenter_set_rotation(p, PV2_ROTATE_90, 0) == ESP_OK);
    assert(pv2_presenter_get_caps(p, &caps) == ESP_OK && caps.draw_layout.width == 2 && caps.draw_layout.height == 4);
    uint32_t x, y;
    assert(pv2_presenter_map_point(p, 3, 0, &x, &y) == ESP_OK && x == 0 && y == 0);
    assert(pv2_presenter_map_point(p, 4, 0, &x, &y) == ESP_ERR_INVALID_ARG);
    assert(pv2_presenter_query_frame(p, receipt, &status) == ESP_OK && status.present_confirmed);
    assert(pv2_presenter_release_receipt(p, receipt) == ESP_OK);
    assert(pv2_presenter_stop(p, 1000) == ESP_OK);
    assert(pv2_presenter_destroy(p) == ESP_OK);
}

int main(void)
{
    rotation_and_confirmation();
    explicit_receipts();
    handoff_and_policy();
    leases_and_ownership();
    receipts_cancel_and_stop();
    async_error_and_recycle();
    puts("PASS presenter: call order, lease identity, producer task, bounded receipts, drain, cancel, stop, async faults");
    return 0;
}
