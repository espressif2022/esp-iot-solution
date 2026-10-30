/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "sync_panel.h"
#include <stdio.h>
#define OK(x) assert((x) == ESP_OK)

static pv2_presenter_t *create(sync_panel_t *s, uint32_t timeout)
{
    pv2_presenter_config_t config = {.backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
            .format = PV2_FORMAT_RGB565, .panel = sync_panel_interface(s),
            .te_window_us = 1000000, .te_transfer_budget_us = 1000
        },
        .backend_kind = PV2_PRESENTER_TE, .timeout_ms = timeout
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
        memset(lease.surface.pixels, 0xab, lease.surface.capacity_bytes);
        pv2_area_t area = {0, y, 3, y + 1};
        OK(pv2_presenter_submit_buffer(p, &lease, &area, 8));
    }
    OK(pv2_presenter_commit_frame(p, NULL));
}

static void destroy(pv2_presenter_t *p, sync_panel_t *s)
{
    OK(pv2_presenter_stop(p, 0));
    OK(pv2_presenter_destroy(p));
    sync_panel_deinit(s);
}

static void pressure(void)
{
    for (unsigned timeout = 0; timeout <= 5; timeout += 5) {
        sync_panel_t s;
        sync_panel_init(&s);
        pv2_presenter_t *p = create(&s, timeout);
        frame(p);
        pv2_frame_request_t request = {0};
        uint64_t start = test_now_us();
        assert(pv2_presenter_begin_next_frame(p, &request) == ESP_ERR_TIMEOUT);
        assert(test_now_us() - start < 500000); /* One bounded admission budget. */
        pv2_presenter_status_t status;
        OK(pv2_presenter_get_status(p, &status));
        assert(!status.frame_active && status.pending_frames == 1 && status.first_error == ESP_OK);
        pthread_t thread = sync_start_worker(&s, p);
        OK(pv2_presenter_drain(p, 1000));
        assert(pthread_join(thread, NULL) == 0);
        OK(pv2_presenter_begin_next_frame(p, &request));
        OK(pv2_presenter_cancel_frame(p));
        destroy(p, &s);
    }
    sync_panel_t s;
    sync_panel_init(&s);
    pv2_presenter_t *p = create(&s, 1000);
    frame(p);
    pthread_t thread = sync_start_worker(&s, p);
    pv2_frame_request_t request = {0};
    OK(pv2_presenter_begin_next_frame(p, &request));
    assert(pthread_join(thread, NULL) == 0);
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.completed_frames == 1 && status.frame_active);
    OK(pv2_presenter_cancel_frame(p));
    destroy(p, &s);
}

static void rejection_and_fault(void)
{
    sync_panel_t s;
    sync_panel_init(&s);
    pv2_presenter_t *p = create(&s, 0);
    frame(p);
    s.panel.reject = true;
    pv2_presenter_notify_te(p, test_now_us());
    assert(pv2_presenter_service(p) == ESP_FAIL);
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.first_error == ESP_OK && status.pending_frames == 1);
    s.panel.reject = false;
    OK(pv2_presenter_service(p));
    assert(s.panel.submitted == 0);
    pthread_t thread = sync_start_worker(&s, p);
    OK(pv2_presenter_drain(p, 1000));
    assert(pthread_join(thread, NULL) == 0);
    frame(p);
    s.uncertain = true;
    pv2_presenter_notify_te_isr(p, test_now_us());
    assert(pv2_presenter_service(p) == ESP_FAIL);
    assert(pv2_presenter_stop(p, 0) == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_destroy(p) == ESP_ERR_INVALID_STATE);
    sync_complete(&s);
    OK(pv2_presenter_stop(p, 0));
    OK(pv2_presenter_get_status(p, &status));
    assert(status.first_error == ESP_FAIL && !status.pending_frames);
    OK(pv2_presenter_destroy(p));
    sync_panel_deinit(&s);
}

static void *wrong_owner(void *context)
{
    assert(pv2_presenter_service(context) == ESP_ERR_INVALID_STATE);
    return NULL;
}

static void io_modes(void)
{
    const pv2_present_mode_t modes[] = {PV2_MODE_NONE, PV2_MODE_TE_SYNC, PV2_MODE_AUTO, PV2_MODE_AUTO};
    for (size_t i = 0; i < 4; i++) {
        sync_panel_t panel;
        sync_panel_init(&panel);
        pv2_presenter_config_t cfg = {.backend = {
                .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
                .format = PV2_FORMAT_RGB565, .panel = sync_panel_interface(&panel),
                .te_window_us = i == 3 ? 0 : 1000000, .te_transfer_budget_us = i == 3 ? 0 : 1000
            },
            .mode = modes[i]
        };
        pv2_presenter_t *p;
        OK(pv2_presenter_create(&cfg, &p));
        pv2_backend_caps_t caps;
        OK(pv2_presenter_get_caps(p, &caps));
        assert(caps.retains_content == (i == 1 || i == 2));
        assert(!caps.supports_present_confirmation && caps.contract == PV2_CONTRACT_PARTITION);
        destroy(p, &panel);
    }
}
int main(void)
{
    pv2_presenter_config_t invalid = {.backend_kind = (pv2_presenter_backend_t)99};
    pv2_presenter_t *p = (void *)1;
    assert(pv2_presenter_create(&invalid, &p) == ESP_ERR_INVALID_ARG && p == NULL);
    io_modes();
    pressure();
    rejection_and_fault();
    sync_panel_t s;
    sync_panel_init(&s);
    p = create(&s, 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, wrong_owner, p) == 0);
    assert(pthread_join(thread, NULL) == 0);
    s.panel.immediate = true;
    for (unsigned i = 0; i < 10; i++) {
        frame(p);
        pv2_presenter_notify_te(p, test_now_us());
        OK(pv2_presenter_service(p));
    }
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(p, &status));
    assert(status.completed_frames == 10 && !status.pending_frames);
    destroy(p, &s);
    puts("Presenter TE: bounded admission, notification thread, retry, fault and stop passed");
    return 0;
}
