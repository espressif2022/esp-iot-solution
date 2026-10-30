/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../src/present_os.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    pv2_event_t *ready;
    pv2_event_t *go;
    unsigned count;
} handshake_t;
static void *producer(void *context)
{
    handshake_t *h = context;
    for (unsigned i = 0; i < h->count; i++) {
        assert(pv2_event_wait(h->go, 1000));
        (void)pv2_event_signal(h->ready, true);
    }
    return NULL;
}
int main(void)
{
    handshake_t h = {.ready = pv2_event_create(), .go = pv2_event_create(), .count = 1000};
    assert(h.ready && h.go);
    assert(!pv2_event_wait(h.ready, 0));
    (void)pv2_event_signal(h.ready, false);
    (void)pv2_event_signal(h.ready, false);
    assert(pv2_event_wait(h.ready, 0)); /* Signal before wait, coalesced. */
    assert(!pv2_event_wait(h.ready, 0));
    uint64_t start = pv2_now_ms();
    assert(!pv2_event_wait(h.ready, 5));
    assert(pv2_now_ms() - start >= 4 && pv2_now_ms() - start < 500);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, producer, &h) == 0);
    for (unsigned i = 0; i < h.count; i++) {
        (void)pv2_event_signal(h.go, false);
        assert(pv2_event_wait(h.ready, 1000));
    }
    assert(pthread_join(thread, NULL) == 0);
    pv2_event_destroy(h.ready);
    pv2_event_destroy(h.go);
    puts("Latched events: pre-signal, coalescing, deadline and 1000 handshakes passed");
    return 0;
}
