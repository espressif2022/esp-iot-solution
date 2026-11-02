/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "present_presenter.h"
#include "present_os.h"
#include "test_panel.h"
#include "scan_panel.h"
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#define OK(x) assert((x) == ESP_OK)
static bool fake_clock = true;
static uint64_t clock_us;
uint64_t __real_pv2_now_us(void);
uint64_t __wrap_pv2_now_us(void)
{
    return fake_clock ? clock_us : __real_pv2_now_us();
}
static size_t allocation_count, fail_allocation;
void *__real_pv2_alloc(size_t);
void *__wrap_pv2_alloc(size_t size)
{
    if (fail_allocation && ++allocation_count == fail_allocation) {
        return NULL;
    }
    return __real_pv2_alloc(size);
}
static void allocation_rollback(void)
{
    for (size_t nth = 1; nth < 128; ++nth) {
        test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
        pv2_presenter_config_t config = {.backend = {.width = 4, .height = 4, .tile_rows = 2,
                                                     .buffer_count = 2, .max_frames = 4, .format = PV2_FORMAT_RGB565,
                                                     .panel = test_panel_interface(&panel)
                                                    }, .transfer_timeout_ms = 5
                                        };
        allocation_count = 0;
        fail_allocation = nth;
        pv2_presenter_t *presenter = NULL;
        esp_err_t error = pv2_presenter_create(&config, &presenter);
        fail_allocation = 0;
        if (error == ESP_OK) {
            OK(pv2_presenter_stop(presenter, 1000));
            OK(pv2_presenter_destroy(presenter));
            return;
        }
        assert(error == ESP_ERR_NO_MEM && !presenter && !panel.callback);
    }
    assert(!"allocation sweep did not reach a successful create");
}
static pv2_presenter_t *create_adapter(pv2_panel_adapter_t adapter, uint32_t timeout)
{
    pv2_presenter_config_t config = {.backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 4,
            .format = PV2_FORMAT_RGB565, .panel = adapter
        },
        .transfer_timeout_ms = timeout
    };
    pv2_presenter_t *presenter;
    OK(pv2_presenter_create(&config, &presenter));
    const pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    return presenter;
}
static pv2_presenter_t *create(test_panel_t *panel, uint32_t timeout)
{
    return create_adapter(test_panel_interface(panel), timeout);
}
static esp_err_t send(pv2_presenter_t *presenter, uint32_t y)
{
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0xab, 16);
    pv2_area_t area = {0, y, 3, y + 1};
    return pv2_presenter_submit_buffer(presenter, &lease, &area, 8);
}
static pv2_presenter_receipt_t seal(pv2_presenter_t *presenter)
{
    pv2_presenter_receipt_t receipt;
    OK(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt));
    return receipt;
}
static void destroy(pv2_presenter_t *presenter)
{
    OK(pv2_presenter_cancel_frame(presenter));
    OK(pv2_presenter_stop(presenter, 1000));
    OK(pv2_presenter_destroy(presenter));
}
static void unsealed_and_late(bool poll_before_done)
{
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    clock_us = 100000;
    pv2_presenter_t *presenter = create(&panel, 5);
    OK(send(presenter, 0));
    clock_us += 5000;
    if (poll_before_done) {
        pv2_presenter_status_t status;
        OK(pv2_presenter_get_status(presenter, &status));
        assert(status.first_error == ESP_ERR_TIMEOUT && status.frame_active);
        pv2_lease_t unused;
        assert(pv2_presenter_acquire_buffer(presenter, &unused) == ESP_ERR_TIMEOUT && !unused.id.value);
        assert(pv2_presenter_stop(presenter, 0) == ESP_ERR_TIMEOUT);
        assert(panel.count == 1 && panel.callback);
    }
    /* Source is read here, after the deadline and after failed stop. */
    test_panel_complete(&panel);
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(presenter, &status));
    assert(status.first_error == ESP_ERR_TIMEOUT && !status.committed_frames);
    destroy(presenter);
}
static void per_push_and_instances(void)
{
    test_panel_t a = {.width = 4, .height = 4, .bpp = 2};
    test_panel_t b = {.width = 4, .height = 4, .bpp = 2};
    clock_us = 200000;
    pv2_presenter_t *first = create(&a, 5);
    OK(send(first, 0));
    clock_us += 4000;
    test_panel_complete(&a); /* First push met its own deadline. */
    OK(send(first, 2));
    pv2_presenter_t *second = create(&b, 5);
    OK(send(second, 0));
    pv2_presenter_receipt_t one = seal(first), two = seal(second);
    clock_us += 4999;
    pv2_frame_status_t status;
    OK(pv2_presenter_query_frame(first, one, &status));
    assert(!status.terminal && !status.source_released && status.result == ESP_OK);
    test_panel_complete(&b); /* The other instance must not inherit A's fault. */
    clock_us += 1;
    OK(pv2_presenter_query_frame(first, one, &status));
    assert(!status.terminal && !status.source_released && status.result == ESP_ERR_TIMEOUT);
    assert(pv2_presenter_release_receipt(first, one) == ESP_ERR_INVALID_STATE);
    OK(pv2_presenter_query_frame(second, two, &status));
    assert(status.terminal && status.result == ESP_OK);
    test_panel_complete(&a);
    OK(pv2_presenter_query_frame(first, one, &status));
    assert(status.terminal && status.source_released && status.result == ESP_ERR_TIMEOUT && !status.present_confirmed);
    OK(pv2_presenter_release_receipt(first, one));
    OK(pv2_presenter_release_receipt(second, two));
    destroy(first);
    destroy(second);
}
static void rejection_and_early_done(void)
{
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2, .reject = true};
    clock_us = 300000;
    pv2_presenter_t *presenter = create(&panel, 5);
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0xab, 16);
    pv2_area_t area = {0, 0, 3, 1};
    assert(pv2_presenter_submit_buffer(presenter, &lease, &area, 8) == ESP_FAIL);
    clock_us += 100000;
    pv2_presenter_status_t status;
    OK(pv2_presenter_get_status(presenter, &status));
    assert(status.first_error == ESP_OK && !panel.count);
    panel.reject = false;
    panel.immediate = true;
    OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 8));
    pv2_presenter_receipt_t receipt = seal(presenter);
    clock_us += 100000;
    pv2_frame_status_t observed;
    OK(pv2_presenter_query_frame(presenter, receipt, &observed));
    assert(observed.terminal && observed.result == ESP_OK);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    destroy(presenter);
    /* Zero retains the old opt-out behavior. */
    panel = (test_panel_t) {
        .width = 4, .height = 4, .bpp = 2
    };
    presenter = create(&panel, 0);
    OK(send(presenter, 0));
    clock_us += 1000000;
    OK(pv2_presenter_get_status(presenter, &status));
    assert(status.first_error == ESP_OK);
    test_panel_complete(&panel);
    destroy(presenter);
}

static pv2_tx_result_t uncertain_submit(void *context, const pv2_surface_t *surface,
                                        const pv2_rect_t *area, uint64_t cookie)
{
    pv2_tx_result_t result = test_panel_submit(context, surface, area, cookie);
    assert(result.acceptance == PV2_TX_ACCEPTED);
    return (pv2_tx_result_t) {
        PV2_TX_UNCERTAIN, ESP_FAIL
    };
}
static void uncertain_retains_sources(void)
{
    clock_us = 600000;
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    pv2_panel_adapter_t adapter = test_panel_interface(&panel);
    adapter.submit = uncertain_submit;
    pv2_presenter_t *presenter = create_adapter(adapter, 5);
    OK(send(presenter, 0)); /* Accepted/uncertain consumes the lease. */
    pv2_presenter_receipt_t receipt = seal(presenter);
    clock_us += 5000;
    pv2_frame_status_t status;
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.result == ESP_FAIL && !status.terminal && !status.source_released);
    assert(pv2_presenter_stop(presenter, 0) == ESP_ERR_TIMEOUT);
    assert(panel.callback && panel.count == 1);
    test_panel_complete(&panel);
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.result == ESP_FAIL && status.terminal && status.source_released);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    destroy(presenter); /* Preserve the first driver failure, not later timeout. */
}
static void late_scanout_confirmation(bool poll_first)
{
    clock_us = 800000;
    scan_panel_t panel = {.stop_timeout = true};
    pv2_presenter_config_t config = {.backend_kind = PV2_PRESENTER_FB, .mode = PV2_MODE_DOUBLE_FULL,
                                     .backend = {.width = 4, .height = 4, .format = PV2_FORMAT_RGB565,
                                                 .max_frames = 4, .panel = scan_interface(&panel)
                                                }, .transfer_timeout_ms = 5
                                    };
    pv2_presenter_t *presenter;
    OK(pv2_presenter_create(&config, &presenter));
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0xab, 32);
    pv2_area_t area = {0, 0, 3, 3};
    OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 8));
    pv2_presenter_receipt_t receipt = seal(presenter);
    OK(pv2_presenter_service(presenter));
    assert(panel.cookie);
    clock_us += 5000;
    pv2_frame_status_t status;
    if (poll_first) {
        OK(pv2_presenter_query_frame(presenter, receipt, &status));
        assert(status.result == ESP_ERR_TIMEOUT && !status.terminal && !status.source_released);
        assert(pv2_presenter_stop(presenter, 0) == ESP_ERR_TIMEOUT);
    }
    scan_confirm(&panel); /* True ownership evidence must still be consumed. */
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.terminal && status.source_released && status.result == ESP_ERR_TIMEOUT && !status.present_confirmed);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    panel.stop_timeout = false;
    destroy(presenter);
}

static void te_wait_is_not_transfer_time(void)
{
    clock_us = 900000;
    test_panel_t panel = {.width = 4, .height = 4, .bpp = 2};
    pv2_presenter_config_t config = {.backend_kind = PV2_PRESENTER_TE,
                                     .backend = {.width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2,
                                                 .max_frames = 4, .format = PV2_FORMAT_RGB565, .panel = test_panel_interface(&panel),
                                                 .te_window_us = 100000, .te_transfer_budget_us = 1000
                                                }, .transfer_timeout_ms = 5
                                    };
    pv2_presenter_t *presenter;
    OK(pv2_presenter_create(&config, &presenter));
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    OK(send(presenter, 0));
    OK(send(presenter, 2));
    pv2_presenter_receipt_t receipt = seal(presenter);
    clock_us += 1000000; /* No edge: still no physical submit. */
    pv2_frame_status_t observed;
    OK(pv2_presenter_query_frame(presenter, receipt, &observed));
    assert(!panel.count && observed.result == ESP_OK && !observed.terminal);
    pv2_presenter_notify_te(presenter, clock_us);
    OK(pv2_presenter_service(presenter));
    assert(panel.count == 1);
    clock_us += 5000;
    OK(pv2_presenter_query_frame(presenter, receipt, &observed));
    assert(observed.result == ESP_ERR_TIMEOUT && !observed.terminal && !observed.source_released);
    test_panel_complete(&panel);
    OK(pv2_presenter_query_frame(presenter, receipt, &observed));
    assert(observed.terminal && observed.source_released && observed.result == ESP_ERR_TIMEOUT);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    destroy(presenter);
}

/* Independent, synchronized sender test: no polling API can trigger expiry
 * before the condition is signaled by the sender's backend FAULT notification. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pv2_panel_event_fn callback;
    void *user;
    uint64_t cookie;
    pv2_surface_t source;
    pv2_task_t sender;
    bool submitted, expired;
} automatic_panel_t;
static esp_err_t automatic_attach(void *user, pv2_panel_event_fn callback, void *context)
{
    automatic_panel_t *panel = user;
    panel->callback = callback;
    panel->user = context;
    return ESP_OK;
}
static pv2_tx_result_t automatic_submit(void *user, const pv2_surface_t *source, const pv2_rect_t *area, uint64_t cookie)
{
    (void)area;
    automatic_panel_t *panel = user;
    pthread_mutex_lock(&panel->lock);
    assert(!panel->cookie);
    panel->cookie = cookie;
    panel->source = *source;
    panel->sender = pv2_current_task();
    panel->submitted = true;
    pthread_cond_broadcast(&panel->condition);
    pthread_mutex_unlock(&panel->lock);
    return (pv2_tx_result_t) {
        PV2_TX_ACCEPTED, ESP_OK
    };
}
static bool automatic_progress(void *user, bool isr)
{
    (void)isr;
    automatic_panel_t *panel = user;
    pthread_mutex_lock(&panel->lock);
    if (panel->submitted && panel->cookie && pv2_same_task(panel->sender, pv2_current_task())) {
        panel->expired = true;
        pthread_cond_broadcast(&panel->condition);
    }
    pthread_mutex_unlock(&panel->lock);
    return false;
}
static esp_err_t automatic_stop(void *user, uint32_t timeout)
{
    (void)timeout;
    automatic_panel_t *panel = user;
    pthread_mutex_lock(&panel->lock);
    esp_err_t error = panel->cookie ? ESP_ERR_TIMEOUT : ESP_OK;
    if (error == ESP_OK) {
        panel->callback = NULL;
    }
    pthread_mutex_unlock(&panel->lock);
    return error;
}
static void automatic_sender(void)
{
    fake_clock = false;
    automatic_panel_t panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER};
    pv2_presenter_config_t config = {.backend = {.width = 4, .height = 4, .tile_rows = 2,
                                                 .buffer_count = 2, .max_frames = 4, .format = PV2_FORMAT_RGB565, .async_copy = true,
                                                 .panel = {&panel, automatic_attach, automatic_submit, automatic_stop},
                                                 .on_progress = automatic_progress, .progress_user = &panel
                                                },
                                     .timeout_ms = 1000, .transfer_timeout_ms = 10, .background_service = true
                                    };
    pv2_presenter_t *presenter;
    OK(pv2_presenter_create(&config, &presenter));
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    OK(send(presenter, 0));
    pv2_presenter_receipt_t receipt = seal(presenter);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&panel.lock);
    while (!panel.expired) {
        assert(!pthread_cond_timedwait(&panel.condition, &panel.lock, &deadline));
    }
    pv2_surface_t retained = panel.source;
    pthread_mutex_unlock(&panel.lock);
    /* A real fault notification arrived with no owner pump/status call. */
    pv2_frame_status_t status;
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.result == ESP_ERR_TIMEOUT && !status.source_released && !status.terminal);
    assert(pv2_presenter_stop(presenter, 1000) == ESP_ERR_TIMEOUT);
    uint16_t pixel;
    memcpy(&pixel, retained.pixels, sizeof(pixel));
    assert(pixel == 0xabab);
    pthread_mutex_lock(&panel.lock);
    pv2_backend_event_t done = {.kind = PV2_EVENT_TRANSFER_DONE, .transaction_cookie = panel.cookie};
    panel.cookie = 0;
    pv2_panel_event_fn callback = panel.callback;
    void *user = panel.user;
    pthread_mutex_unlock(&panel.lock);
    (void)callback(user, &done, false);
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.terminal && status.source_released && status.result == ESP_ERR_TIMEOUT);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    destroy(presenter);
    pthread_mutex_destroy(&panel.lock);
    pthread_cond_destroy(&panel.condition);
}
int main(void)
{
    allocation_rollback();
    unsealed_and_late(false);
    unsealed_and_late(true);
    per_push_and_instances();
    rejection_and_early_done();
    uncertain_retains_sources();
    late_scanout_confirmation(false);
    late_scanout_confirmation(true);
    te_wait_is_not_transfer_time();
    automatic_sender();
    puts("transfer deadlines: physical-submit origin, retained DMA, per-push isolation, retry, early/late completion and sender wake passed");
}
