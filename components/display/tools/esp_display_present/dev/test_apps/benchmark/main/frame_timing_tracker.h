/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { BENCH_TIMING_FRAMES = 32, BENCH_TIMING_BUCKETS = 512 };
typedef struct {
    uint64_t sequence, started_us, cookie, completed_us;
    bool measured, committed, accepted, completed, failed;
} bench_timed_frame_t;
typedef struct {
    bench_timed_frame_t frames[BENCH_TIMING_FRAMES];
    uint32_t buckets[BENCH_TIMING_BUCKETS];
    uint64_t next_sequence, begin_us, samples, sum_us, max_us, excluded, errors;
    bool begin_valid;
} bench_frame_timing_tracker_t;

/* The benchmark owner and completion callback serialize these functions. The
 * FIFO order is the FB backend's commit/submit order, including early callbacks. */
static inline bench_timed_frame_t *bench_timing_find(bench_frame_timing_tracker_t *t, uint64_t sequence)
{
    for (unsigned i = 0; sequence && i < BENCH_TIMING_FRAMES; ++i) {
        if (t->frames[i].sequence == sequence) {
            return &t->frames[i];
        }
    }
    return NULL;
}

static inline bench_timed_frame_t *bench_timing_cookie(bench_frame_timing_tracker_t *t, uint64_t cookie)
{
    for (unsigned i = 0; cookie && i < BENCH_TIMING_FRAMES; ++i) {
        if (t->frames[i].sequence && t->frames[i].cookie == cookie) {
            return &t->frames[i];
        }
    }
    return NULL;
}

static inline void bench_timing_finish(bench_frame_timing_tracker_t *t, bench_timed_frame_t *frame)
{
    if (!frame->committed || !frame->accepted || !frame->completed) {
        return;
    }
    if (frame->failed) {
        ++t->errors;
    } else if (frame->measured && t->samples < UINT32_MAX) {
        uint64_t elapsed = frame->completed_us - frame->started_us;
        unsigned bucket = elapsed / 1000 < BENCH_TIMING_BUCKETS ? elapsed / 1000 : BENCH_TIMING_BUCKETS - 1;
        ++t->buckets[bucket];
        ++t->samples;
        t->sum_us += elapsed;
        if (elapsed > t->max_us) {
            t->max_us = elapsed;
        }
    } else {
        ++t->excluded;
    }
    *frame = (bench_timed_frame_t) {0};
}

static inline uint64_t bench_timing_commit_start(bench_frame_timing_tracker_t *t)
{
    for (unsigned i = 0; i < BENCH_TIMING_FRAMES; ++i) {
        if (!t->frames[i].sequence) {
            t->frames[i] = (bench_timed_frame_t) {
                .sequence = ++t->next_sequence,
                .started_us = t->begin_us,
                .measured = t->begin_valid,
            };
            return t->frames[i].sequence;
        }
    }
    ++t->errors;
    return 0;
}

static inline void bench_timing_commit_done(bench_frame_timing_tracker_t *t, uint64_t sequence, bool success)
{
    bench_timed_frame_t *frame = bench_timing_find(t, sequence);
    if (!frame) {
        return;
    }
    if (success) {
        frame->committed = true;
        t->begin_valid = false;
        bench_timing_finish(t, frame);
    } else {
        *frame = (bench_timed_frame_t) {0};
    }
}

static inline void bench_timing_submit_start(bench_frame_timing_tracker_t *t, uint64_t cookie)
{
    bench_timed_frame_t *oldest = NULL;
    for (unsigned i = 0; i < BENCH_TIMING_FRAMES; ++i) {
        bench_timed_frame_t *frame = &t->frames[i];
        if (frame->sequence && !frame->cookie && (!oldest || frame->sequence < oldest->sequence)) {
            oldest = frame;
        }
    }
    if (oldest) {
        oldest->cookie = cookie;
    } else {
        ++t->errors;
    }
}

static inline void bench_timing_submit_done(bench_frame_timing_tracker_t *t, uint64_t cookie, bool accepted, bool success)
{
    bench_timed_frame_t *frame = bench_timing_cookie(t, cookie);
    if (!frame) {
        return;
    }
    if (!accepted) {
        frame->cookie = 0;
        frame->completed = false;
        frame->failed = false;
        return;
    }
    frame->accepted = true;
    frame->failed |= !success;
    bench_timing_finish(t, frame);
}

static inline void bench_timing_complete(bench_frame_timing_tracker_t *t, uint64_t cookie, uint64_t now, bool success)
{
    bench_timed_frame_t *frame = bench_timing_cookie(t, cookie);
    if (frame && !frame->completed) {
        frame->completed_us = now;
        frame->completed = true;
        frame->failed |= !success;
        bench_timing_finish(t, frame);
    }
}

static inline uint64_t bench_timing_p99(const bench_frame_timing_tracker_t *t)
{
    uint64_t rank = t->samples - t->samples / 100, cumulative = 0;
    for (unsigned i = 0; rank && i < BENCH_TIMING_BUCKETS; ++i) {
        cumulative += t->buckets[i];
        if (cumulative >= rank) {
            uint64_t upper = (i + 1) * 1000;
            return i == BENCH_TIMING_BUCKETS - 1 || upper > t->max_us ? t->max_us : upper;
        }
    }
    return 0;
}
