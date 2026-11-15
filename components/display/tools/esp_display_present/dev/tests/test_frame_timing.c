/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "../test_apps/benchmark/main/frame_timing_tracker.h"
#include <assert.h>
#include <stdio.h>

static void begin(bench_frame_timing_tracker_t *t, uint64_t started)
{
    t->begin_us = started;
    t->begin_valid = true;
}

static void waiting_and_scanout(void)
{
    bench_frame_timing_tracker_t t = {0};
    begin(&t, 1000); /* Start before a 20 ms buffer wait and 10 ms render. */
    uint64_t frame = bench_timing_commit_start(&t);
    bench_timing_commit_done(&t, frame, true);
    bench_timing_submit_start(&t, 101);
    bench_timing_submit_done(&t, 101, true, true);
    assert(t.samples == 0);
    bench_timing_complete(&t, 101, 51000, true); /* Includes 20 ms scanout wait. */
    assert(t.samples == 1 && t.sum_us == 50000 && bench_timing_p99(&t) == 50000);
    bench_timing_complete(&t, 101, 99000, true);
    assert(t.samples == 1); /* A repeated ISR cannot sample the frame twice. */
}

static void early_reject_and_retry(void)
{
    bench_frame_timing_tracker_t t = {0};
    begin(&t, 0);
    uint64_t frame = bench_timing_commit_start(&t);
    bench_timing_submit_start(&t, 1);
    bench_timing_complete(&t, 1, 500, true);
    bench_timing_submit_done(&t, 1, false, false);
    bench_timing_commit_done(&t, frame, true);
    assert(t.samples == 0);
    bench_timing_submit_start(&t, 2);
    bench_timing_complete(&t, 1, 900, true); /* Rejected submission's late callback. */
    bench_timing_complete(&t, 2, 1200, true);
    assert(t.samples == 0);
    bench_timing_submit_done(&t, 2, true, true);
    assert(t.samples == 1 && t.max_us == 1200);

    begin(&t, 2000);
    frame = bench_timing_commit_start(&t);
    bench_timing_submit_start(&t, 3);
    bench_timing_complete(&t, 3, 3000, true);
    bench_timing_submit_done(&t, 3, true, true);
    assert(t.samples == 1); /* Driver completion precedes end_ex resolution. */
    bench_timing_commit_done(&t, frame, false);
    assert(t.samples == 1 && !bench_timing_cookie(&t, 3));
}

static void fifo_failures_and_percentile(void)
{
    bench_frame_timing_tracker_t t = {0};
    for (unsigned i = 0; i < 100; ++i) {
        begin(&t, i * 1000000ULL);
        uint64_t frame = bench_timing_commit_start(&t);
        bench_timing_commit_done(&t, frame, true);
        bench_timing_submit_start(&t, i + 1);
        bench_timing_submit_done(&t, i + 1, true, true);
        bench_timing_complete(&t, i + 1, t.begin_us + (i == 99 ? 300000 : i == 98 ? 24500 : 1000), true);
    }
    assert(t.samples == 100 && bench_timing_p99(&t) == 25000);
    begin(&t, 100000000);
    uint64_t frame = bench_timing_commit_start(&t);
    bench_timing_commit_done(&t, frame, true);
    bench_timing_submit_start(&t, 101);
    bench_timing_submit_done(&t, 101, true, false);
    bench_timing_complete(&t, 101, 100200000, true);
    assert(t.samples == 100 && t.errors == 1);

    memset(&t, 0, sizeof(t));
    uint64_t unmeasured = bench_timing_commit_start(&t);
    bench_timing_commit_done(&t, unmeasured, true);
    begin(&t, 1000);
    frame = bench_timing_commit_start(&t);
    bench_timing_commit_done(&t, frame, true);
    bench_timing_submit_start(&t, 5);
    bench_timing_submit_done(&t, 5, true, true);
    bench_timing_complete(&t, 5, 1500, true);
    assert(t.samples == 0 && t.excluded == 1);
    bench_timing_submit_start(&t, 6);
    bench_timing_submit_done(&t, 6, true, true);
    bench_timing_complete(&t, 6, 2000, true);
    assert(t.samples == 1 && t.sum_us == 1000);
}

int main(void)
{
    waiting_and_scanout();
    early_reject_and_retry();
    fifo_failures_and_percentile();
    puts("frame timing: begin wait, accepted scanout, early/rejected/duplicate callbacks and percentile passed");
}
