/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_pixel_engine.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pv2_pixel_engine_t *make_engine(size_t jobs)
{
    pv2_pixel_engine_t *e;
    pv2_pixel_config_t config = { .max_jobs = jobs };
    assert(pv2_pixel_create(&config, &e) == ESP_OK);
    return e;
}

static void destroy(pv2_pixel_engine_t *e)
{
    assert(pv2_pixel_stop(e, 0) == ESP_OK);
    assert(pv2_pixel_destroy(e) == ESP_OK);
}

static void pixels_and_padding(bool asynchronous)
{
    /* Independently enumerated expected clockwise rotations of 3 x 2. */
    const uint8_t expected[4][6] = {
        {11, 12, 13, 21, 22, 23}, {21, 11, 22, 12, 23, 13},
        {23, 22, 21, 13, 12, 11}, {13, 23, 12, 22, 11, 21},
    };
    pv2_pixel_engine_t *e;
    pv2_pixel_config_t config = {.max_jobs = 1, .asynchronous = asynchronous};
    assert(pv2_pixel_create(&config, &e) == ESP_OK);
    for (int format = PV2_FORMAT_RGB565; format <= PV2_FORMAT_RGB888; ++format) {
        size_t bpp = format == PV2_FORMAT_RGB565 ? 2 : 3;
        for (int swap = 0; swap <= (format == PV2_FORMAT_RGB565); ++swap) {
            for (int rot = PV2_ROTATE_0; rot <= PV2_ROTATE_270; ++rot) {
                uint8_t src[128], snapshot[128], dst[160], reference[160];
                memset(src, 0xA5, sizeof(src));
                memset(dst, 0xCD, sizeof(dst));
                memset(reference, 0xCD, sizeof(reference));
                for (size_t y = 0; y < 2; ++y) {
                    for (size_t x = 0; x < 3; ++x) {
                        size_t at = (y + 1) * 6 * bpp + (x + 1) * bpp;
                        src[at] = (uint8_t)(11 + y * 10 + x);
                        src[at + 1] = 0x71;
                        if (bpp == 3) {
                            src[at + 2] = 0x93;
                        }
                    }
                }
                memcpy(snapshot, src, sizeof(src));
                uint32_t w = (rot % 2) ? 2 : 3, h = (rot % 2) ? 3 : 2;
                for (uint32_t y = 0; y < h; ++y) {
                    for (uint32_t x = 0; x < w; ++x) {
                        size_t at = (y + 2) * 8 * bpp + (x + 1) * bpp;
                        reference[at] = swap ? 0x71 : expected[rot][y * w + x];
                        reference[at + 1] = swap ? expected[rot][y * w + x] : 0x71;
                        if (bpp == 3) {
                            reference[at + 2] = 0x93;
                        }
                    }
                }
                pv2_copy_request_t r = {
                    .source = { src, sizeof(src), { 5, 4, 6 * bpp, format, PV2_ORDER_NATIVE } },
                    .destination = { dst, sizeof(dst), { 6, 6, 8 * bpp, format, swap } },
                    .source_area = { 1, 1, 3, 2 }, .destination_x = 1, .destination_y = 2,
                    .rotation = rot,
                };
                pv2_copy_ticket_t ticket;
                assert(pv2_pixel_submit(e, &r, &ticket) == ESP_OK);
                pv2_copy_status_t status;
                assert(pv2_pixel_wait(e, ticket, UINT32_MAX, &status) == ESP_OK);
                assert(status.state == PV2_COPY_SUCCEEDED && status.result == ESP_OK);
                assert(memcmp(src, snapshot, sizeof(src)) == 0);
                assert(memcmp(dst, reference, sizeof(dst)) == 0);
                assert(pv2_pixel_release(e, ticket) == ESP_OK);
            }
        }
    }
    assert(pv2_pixel_stop(e, UINT32_MAX) == ESP_OK);
    assert(pv2_pixel_destroy(e) == ESP_OK);
}

static void validation_and_records(void)
{
    uint8_t src[32] = {0}, dst[32];
    memset(dst, 0xB6, sizeof(dst));
    pv2_copy_request_t valid = {
        .source = { src, sizeof(src), { 2, 2, 4, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE } },
        .destination = { dst, sizeof(dst), { 2, 2, 4, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE } },
        .source_area = { 0, 0, 2, 2 },
    };
    pv2_pixel_engine_t *a = make_engine(1), *b = make_engine(1);
    for (int variant = 0; variant < 10; ++variant) {
        pv2_copy_request_t r = valid;
        switch (variant) {
        case 0:
            r.source.layout.stride_bytes = 5;
            break;
        case 1:
            r.destination.layout.stride_bytes = 5;
            break;
        case 2:
            r.source.capacity_bytes = 7;
            break;
        case 3:
            r.destination.capacity_bytes = 7;
            break;
        case 4:
            r.destination.pixels = src + 2;
            break;
        case 5:
            r.source_area.x = UINT32_MAX;
            break;
        case 6:
            r.destination_x = UINT32_MAX;
            break;
        case 7:
            r.source.layout.stride_bytes = SIZE_MAX - 1;
            r.source.layout.height = 3;
            break;
        case 8:
            r.rotation = (pv2_rotation_t)99;
            break;
        case 9:
            r.source.layout.format = (pv2_pixel_format_t)99;
            break;
        }
        pv2_copy_ticket_t t = {123};
        assert(pv2_pixel_submit(a, &r, &t) == ESP_ERR_INVALID_ARG);
        assert(t.value == 0);
        for (size_t i = 0; i < sizeof(dst); ++i) {
            assert(dst[i] == 0xB6);
        }
    }
    pv2_copy_ticket_t first, second;
    assert(pv2_pixel_submit(a, &valid, &first) == ESP_OK);
    assert(pv2_pixel_submit(a, &valid, &second) == ESP_ERR_NO_MEM);
    assert(second.value == 0);
    pv2_copy_status_t status;
    assert(pv2_pixel_query(b, first, &status) == ESP_ERR_INVALID_STATE);
    assert(pv2_pixel_release(a, first) == ESP_OK);
    assert(pv2_pixel_submit(a, &valid, &second) == ESP_OK);
    assert(first.value != second.value);
    assert(pv2_pixel_query(a, first, &status) == ESP_ERR_INVALID_STATE);
    assert(pv2_pixel_stop(a, 0) == ESP_OK);
    assert(pv2_pixel_destroy(a) == ESP_ERR_INVALID_STATE);
    assert(pv2_pixel_submit(a, &valid, &first) == ESP_ERR_INVALID_STATE);
    assert(pv2_pixel_release(a, second) == ESP_OK);
    destroy(a);
    destroy(b);
}

static bool on_pixel_done(void *user, pv2_copy_ticket_t ticket,
                          const pv2_copy_status_t *status, bool from_isr)
{
    size_t *calls = user;
    assert(!from_isr);
    assert(ticket.value != 0);
    assert(status && status->state == PV2_COPY_SUCCEEDED && status->result == ESP_OK);
    ++(*calls);
    return false;
}

static void in_place_and_events(void)
{
    size_t events = 0;
    pv2_pixel_engine_t *e;
    pv2_pixel_config_t config = { .max_jobs = 2, .on_event = on_pixel_done, .user = &events };
    assert(pv2_pixel_create(&config, &e) == ESP_OK);

    uint8_t buf[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    pv2_copy_request_t r = {
        .source = { buf, sizeof(buf), { 2, 2, 4, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE } },
        .destination = { buf, sizeof(buf), { 2, 2, 4, PV2_FORMAT_RGB565, PV2_ORDER_SWAPPED } },
        .source_area = { 0, 0, 2, 2 },
        .destination_x = 0, .destination_y = 0, .rotation = PV2_ROTATE_0,
    };
    pv2_copy_ticket_t t;
    assert(pv2_pixel_submit(e, &r, &t) == ESP_OK);
    assert(events == 1);
    const uint8_t expected[8] = { 0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77 };
    assert(memcmp(buf, expected, sizeof(buf)) == 0);
    assert(pv2_pixel_release(e, t) == ESP_OK);

    /* In-place with rotation must be rejected. */
    r.rotation = PV2_ROTATE_180;
    assert(pv2_pixel_submit(e, &r, &t) == ESP_ERR_INVALID_ARG);
    destroy(e);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool entered, leave;
    pv2_pixel_engine_t *engine;
    pv2_copy_request_t request;
} callback_gate_t;

static bool blocking_callback(void *user, pv2_copy_ticket_t ticket,
                              const pv2_copy_status_t *status, bool isr)
{
    callback_gate_t *gate = user;
    assert(!isr && status->state == PV2_COPY_SUCCEEDED);
    pv2_copy_status_t queried;
    assert(pv2_pixel_query(gate->engine, ticket, &queried) == ESP_OK);
    assert(pv2_pixel_release(gate->engine, ticket) == ESP_OK);
    /* Neither self-stop nor recursive submit may deadlock this callback. */
    assert(pv2_pixel_stop(gate->engine, UINT32_MAX) == ESP_ERR_INVALID_STATE);
    pv2_copy_ticket_t nested;
    assert(pv2_pixel_submit(gate->engine, &gate->request, &nested) == ESP_ERR_NO_MEM);
    pthread_mutex_lock(&gate->lock);
    gate->entered = true;
    pthread_cond_signal(&gate->changed);
    while (!gate->leave) {
        pthread_cond_wait(&gate->changed, &gate->lock);
    }
    pthread_mutex_unlock(&gate->lock);
    return false;
}

static void *callback_submitter(void *arg)
{
    callback_gate_t *gate = arg;
    pv2_copy_ticket_t ticket;
    assert(pv2_pixel_submit(gate->engine, &gate->request, &ticket) == ESP_OK);
    return NULL;
}

static void callback_lifetime(bool asynchronous)
{
    uint8_t src[2] = {0x12, 0x34}, dst[2] = {0};
    callback_gate_t gate = { .lock = PTHREAD_MUTEX_INITIALIZER,
                             .changed = PTHREAD_COND_INITIALIZER,
                             .request = { .source = { src, 2, {1, 1, 2, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE} },
                                          .destination = { dst, 2, {1, 1, 2, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE} },
                                          .source_area = {0, 0, 1, 1}
                                        }
                           };
    pv2_pixel_config_t config = { .max_jobs = 1, .on_event = blocking_callback, .user = &gate, .asynchronous = asynchronous };
    assert(pv2_pixel_create(&config, &gate.engine) == ESP_OK);
    pthread_t submitter;
    assert(pthread_create(&submitter, NULL, callback_submitter, &gate) == 0);
    pthread_mutex_lock(&gate.lock);
    while (!gate.entered) {
        pthread_cond_wait(&gate.changed, &gate.lock);
    }
    pthread_mutex_unlock(&gate.lock);
    /* Ticket is already recycled, but user callback still owns its context. */
    assert(pv2_pixel_stop(gate.engine, 0) == ESP_ERR_TIMEOUT);
    pthread_mutex_lock(&gate.lock);
    gate.leave = true;
    pthread_cond_signal(&gate.changed);
    pthread_mutex_unlock(&gate.lock);
    assert(pthread_join(submitter, NULL) == 0);
    assert(memcmp(src, dst, 2) == 0);
    assert(pv2_pixel_stop(gate.engine, UINT32_MAX) == ESP_OK);
    assert(pv2_pixel_destroy(gate.engine) == ESP_OK);
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.lock);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool entered, leave;
    unsigned calls;
} executor_gate_t;

static esp_err_t delayed_executor(void *user, const pv2_copy_request_t *r)
{
    executor_gate_t *gate = user;
    pthread_mutex_lock(&gate->lock);
    gate->calls++;
    gate->entered = true;
    pthread_cond_broadcast(&gate->changed);
    while (!gate->leave) {
        pthread_cond_wait(&gate->changed, &gate->lock);
    }
    pthread_mutex_unlock(&gate->lock);
    /* Test executor intentionally supports only these two-byte requests. */
    memcpy(r->destination.pixels, r->source.pixels, 2);
    return ESP_OK;
}

static void queued_lifetime(void)
{
    executor_gate_t gate = {.lock = PTHREAD_MUTEX_INITIALIZER,
                            .changed = PTHREAD_COND_INITIALIZER
                           };
    uint8_t src[3][2] = {{1, 2}, {3, 4}, {5, 6}}, dst[2] = {0};
    pv2_pixel_config_t cfg = {.max_jobs = 3, .asynchronous = true,
                              .execute = delayed_executor, .execute_user = &gate
                             };
    pv2_pixel_engine_t *e;
    assert(pv2_pixel_create(&cfg, &e) == ESP_OK);
    pv2_copy_request_t r = {
        .source = {src[0], 2, {1, 1, 2, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}},
        .destination = {dst, 2, {1, 1, 2, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE}},
        .source_area = {0, 0, 1, 1}
    };
    pv2_copy_ticket_t tickets[3], extra;
    assert(pv2_pixel_submit(e, &r, &tickets[0]) == ESP_OK);
    pthread_mutex_lock(&gate.lock);
    while (!gate.entered) {
        pthread_cond_wait(&gate.changed, &gate.lock);
    }
    pthread_mutex_unlock(&gate.lock);
    for (unsigned i = 1; i < 3; ++i) {
        r.source.pixels = src[i];
        assert(pv2_pixel_submit(e, &r, &tickets[i]) == ESP_OK);
    }
    assert(pv2_pixel_submit(e, &r, &extra) == ESP_ERR_NO_MEM && !extra.value);
    /* Mutating the descriptor after submit must not alter queued requests. */
    r.source.pixels = NULL;
    pv2_copy_status_t status;
    assert(pv2_pixel_wait(e, tickets[0], 2, &status) == ESP_ERR_TIMEOUT);
    assert(status.state == PV2_COPY_RUNNING);
    assert(pv2_pixel_cancel(e, tickets[2], 0, &status) == ESP_ERR_TIMEOUT);
    assert(pv2_pixel_release(e, tickets[2]) == ESP_ERR_INVALID_STATE);
    assert(pv2_pixel_stop(e, 0) == ESP_ERR_TIMEOUT);
    assert(pv2_pixel_destroy(e) == ESP_ERR_INVALID_STATE);
    r.source.pixels = src[0];
    assert(pv2_pixel_submit(e, &r, &extra) == ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&gate.lock);
    gate.leave = true;
    pthread_cond_broadcast(&gate.changed);
    pthread_mutex_unlock(&gate.lock);
    assert(pv2_pixel_stop(e, UINT32_MAX) == ESP_OK);
    assert(gate.calls == 2);
    assert(memcmp(dst, src[1], 2) == 0); /* FIFO; canceled third never wrote. */
    assert(pv2_pixel_destroy(e) == ESP_ERR_INVALID_STATE); /* receipts survive */
    for (unsigned i = 0; i < 3; ++i) {
        assert(pv2_pixel_wait(e, tickets[i], 0, &status) == ESP_OK);
        assert(status.result == (i == 2 ? ESP_ERR_INVALID_STATE : ESP_OK));
        assert(pv2_pixel_release(e, tickets[i]) == ESP_OK);
        assert(pv2_pixel_query(e, tickets[i], &status) == ESP_ERR_INVALID_STATE);
    }
    assert(pv2_pixel_destroy(e) == ESP_OK);
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.lock);
}

int main(void)
{
    pixels_and_padding(false);
    pixels_and_padding(true);
    queued_lifetime();
    validation_and_records();
    in_place_and_events();
    callback_lifetime(false);
    callback_lifetime(true);
    puts("PASS pixel: RGB565/RGB888, all rotations, swap, in-place, padding, source integrity, invalid layouts, token ownership");
    return 0;
}
