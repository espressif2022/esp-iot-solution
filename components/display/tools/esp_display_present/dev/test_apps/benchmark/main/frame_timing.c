/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "bench_api.h"
#include "frame_timing_tracker.h"
#include "core/present_engine.h"
#include "platform/present_os.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <inttypes.h>

/* Test-only, one display at a time. Internal storage is safe in the panel ISR
 * and identical in both A/B binaries. No production allocation or callback. */
static struct {
    present_lock_t lock;
    bool initialized, enabled;
    esp_display_present_panel_adapter_t original;
    esp_display_present_panel_event_fn callback;
    void *user;
    bench_frame_timing_tracker_t tracker;
} timing;

void bench_frame_timing_reset(void)
{
    present_lock_enter(&timing.lock, false);
    memset(&timing.tracker, 0, sizeof(timing.tracker));
    timing.enabled = true;
    present_lock_exit(&timing.lock, false);
}

void bench_frame_timing_begin(uint64_t started_us, esp_err_t result)
{
    present_lock_enter(&timing.lock, false);
    if (timing.enabled && result == ESP_OK) {
        timing.tracker.begin_us = started_us;
        timing.tracker.begin_valid = true;
    }
    present_lock_exit(&timing.lock, false);
}

uint64_t bench_frame_timing_commit_start(void)
{
    present_lock_enter(&timing.lock, false);
    uint64_t sequence = timing.enabled ? bench_timing_commit_start(&timing.tracker) : 0;
    present_lock_exit(&timing.lock, false);
    return sequence;
}

void bench_frame_timing_commit_done(uint64_t sequence, esp_err_t result)
{
    present_lock_enter(&timing.lock, false);
    bench_timing_commit_done(&timing.tracker, sequence, result == ESP_OK);
    present_lock_exit(&timing.lock, false);
}

static bool timed_event(void *context, const esp_display_present_backend_event_t *event, bool isr)
{
    (void)context;
    uint64_t now = esp_timer_get_time();
    present_lock_enter(&timing.lock, isr);
    if (timing.enabled) {
        if (event->kind == ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED) {
            bench_timing_complete(&timing.tracker, event->transaction_cookie, now, event->result == ESP_OK);
        } else if (event->kind == ESP_DISPLAY_PRESENT_EVENT_FAULT) {
            for (unsigned i = 0; i < BENCH_TIMING_FRAMES; ++i) {
                bench_timed_frame_t *frame = &timing.tracker.frames[i];
                if (frame->sequence && (!event->transaction_cookie || frame->cookie == event->transaction_cookie)) {
                    frame->failed = true;
                }
            }
        }
    }
    present_lock_exit(&timing.lock, isr);
    return timing.callback(timing.user, event, isr);
}

static esp_err_t timed_attach(void *context, esp_display_present_panel_event_fn callback, void *user)
{
    (void)context;
    timing.callback = callback;
    timing.user = user;
    return timing.original.attach(timing.original.context, timed_event, NULL);
}

static esp_display_present_tx_result_t timed_submit(void *context, const esp_display_present_surface_t *surface,
                                                    const esp_display_present_rect_t *area, uint64_t cookie)
{
    (void)context;
    present_lock_enter(&timing.lock, false);
    if (timing.enabled) {
        bench_timing_submit_start(&timing.tracker, cookie);
    }
    present_lock_exit(&timing.lock, false);
    esp_display_present_tx_result_t result = timing.original.submit(timing.original.context, surface, area, cookie);
    present_lock_enter(&timing.lock, false);
    if (timing.enabled) {
        bench_timing_submit_done(&timing.tracker, cookie, result.acceptance != ESP_DISPLAY_PRESENT_TX_REJECTED,
                                 result.result == ESP_OK);
    }
    present_lock_exit(&timing.lock, false);
    return result;
}

static esp_err_t timed_quiesce(void *context, uint32_t timeout)
{
    (void)context;
    return timing.original.quiesce(timing.original.context, timeout);
}

esp_err_t __real_esp_display_present_engine_create(const esp_display_present_engine_config_t *, esp_display_present_engine_t **);
esp_err_t __wrap_esp_display_present_engine_create(const esp_display_present_engine_config_t *config,
                                                   esp_display_present_engine_t **out)
{
    if (!timing.initialized) {
        if (!present_lock_init(&timing.lock)) {
            return ESP_ERR_NO_MEM;
        }
        timing.initialized = true;
    }
    timing.enabled = false;
    if (!config || config->backend_kind != ESP_DISPLAY_PRESENT_ENGINE_FB) {
        return __real_esp_display_present_engine_create(config, out);
    }
    esp_display_present_engine_config_t copy = *config;
    timing.original = copy.backend.panel;
    copy.backend.panel = (esp_display_present_panel_adapter_t) {
        .attach = timed_attach, .submit = timed_submit, .quiesce = timed_quiesce,
    };
    return __real_esp_display_present_engine_create(&copy, out);
}

#ifndef BENCH_PROFILE
esp_err_t __real_esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *, const esp_display_present_frame_request_t *);
esp_err_t __wrap_esp_display_present_engine_begin_next_frame(esp_display_present_engine_t *engine,
                                                             const esp_display_present_frame_request_t *request)
{
    uint64_t started = esp_timer_get_time();
    esp_err_t result = __real_esp_display_present_engine_begin_next_frame(engine, request);
    bench_frame_timing_begin(started, result);
    return result;
}

esp_err_t __real_esp_display_present_engine_commit_frame(esp_display_present_engine_t *, const esp_display_present_frame_coverage_t *);
esp_err_t __wrap_esp_display_present_engine_commit_frame(esp_display_present_engine_t *engine,
                                                         const esp_display_present_frame_coverage_t *coverage)
{
    uint64_t sequence = bench_frame_timing_commit_start();
    esp_err_t result = __real_esp_display_present_engine_commit_frame(engine, coverage);
    bench_frame_timing_commit_done(sequence, result);
    return result;
}
#endif

void bench_frame_timing_report(const char *label)
{
    present_lock_enter(&timing.lock, false);
    unsigned pending = 0;
    for (unsigned i = 0; i < BENCH_TIMING_FRAMES; ++i) {
        pending += timing.tracker.frames[i].sequence != 0;
    }
    uint64_t samples = timing.tracker.samples, sum = timing.tracker.sum_us, maximum = timing.tracker.max_us;
    uint64_t excluded = timing.tracker.excluded, errors = timing.tracker.errors;
    uint32_t overflows = timing.tracker.buckets[BENCH_TIMING_BUCKETS - 1];
    uint64_t p99 = bench_timing_p99(&timing.tracker);
    timing.enabled = false;
    present_lock_exit(&timing.lock, false);
    ESP_LOGI("FRAME_TIMING", "label=%s samples=%" PRIu64 " begin_to_scanout_p99_us=%" PRIu64
             " avg_us=%" PRIu64 " max_us=%" PRIu64 " excluded=%" PRIu64 " errors=%" PRIu64
             " pending=%u histogram_overflows=%" PRIu32 " overflow_ge_us=%u instrumentation_bytes=%u",
             label, samples, p99, samples ? sum / samples : 0, maximum, excluded, errors, pending,
             overflows, (BENCH_TIMING_BUCKETS - 1) * 1000, (unsigned)sizeof(timing));
    ESP_ERROR_CHECK(!samples || errors || pending ? ESP_ERR_INVALID_STATE : ESP_OK);
}
