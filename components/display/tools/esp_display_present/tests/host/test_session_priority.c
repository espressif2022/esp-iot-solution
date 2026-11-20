/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "core/present_engine.h"
#include "core/present_control.h"
#include "pixel/present_pixel_ops.h"
#include "platform/present_os.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x) == ESP_OK)
static _Thread_local unsigned task_priority = 6;
static atomic_uint backend_priority, copy_priority_updates;
unsigned __wrap_present_task_priority(void)
{
    return task_priority;
}
void __real_present_task_set_priority(unsigned);
void __wrap_present_task_set_priority(unsigned p)
{
    task_priority = p;
    atomic_fetch_add(&copy_priority_updates, 1);
    __real_present_task_set_priority(p);
}
present_worker_t *__real_present_worker_create(const char *, present_worker_entry_fn, void *, uint32_t, unsigned);
present_worker_t *__wrap_present_worker_create(const char *n, present_worker_entry_fn f, void *u, uint32_t s, unsigned p)
{
    if (!strcmp(n, "present_backend")) {
        atomic_store(&backend_priority, p);
    }
    return __real_present_worker_create(n, f, u, s, p);
}
void __real_present_worker_set_priority(present_worker_t *, unsigned);
void __wrap_present_worker_set_priority(present_worker_t *w, unsigned p)
{
    atomic_store(&backend_priority, p);
    __real_present_worker_set_priority(w, p);
}
typedef struct {
    esp_display_present_engine_t *engine;
    esp_display_present_panel_event_fn callback;
    void *user;
    atomic_uint expected, copies, submissions;
    bool te;
} fixture_t;
static esp_err_t attach(void *u, esp_display_present_panel_event_fn f, void *v)
{
    fixture_t *m = u;
    m->callback = f;
    m->user = v;
    return ESP_OK;
}
static esp_err_t quiesce(void *u, uint32_t t)
{
    (void)u;
    (void)t;
    return ESP_OK;
}
static esp_display_present_tx_result_t submit(void *u, const esp_display_present_surface_t *s,
                                              const esp_display_present_rect_t *a, uint64_t cookie)
{
    fixture_t *m = u; (void)a;
    for (size_t i = 0; i < s->capacity_bytes; i++) {
        assert(((uint8_t *)s->pixels)[i] == 0xab);
    }
    atomic_fetch_add(&m->submissions, 1);
    esp_display_present_backend_event_t e = {.kind = m->te ? ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE : ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED,
                                             .transaction_cookie = cookie
                                            };
    (void)m->callback(m->user, &e, false);
    return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_ACCEPTED, ESP_OK
    };
}
static esp_err_t execute(void *u, const esp_display_present_copy_request_t *request)
{
    fixture_t *m = u;
    assert(task_priority == atomic_load(&m->expected));
    atomic_fetch_add(&m->copies, 1);
    return esp_display_present_pixel_execute_cpu(request);
}
static esp_err_t admit(void *u, size_t n, uint64_t previous, uint64_t *edge)
{
    (void)u;
    (void)n;
    *edge = previous + 1;
    return ESP_OK;
}
static void frame(fixture_t *m)
{
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(m->engine, &request));
    /* A failed in-frame rebind must not publish a new copy priority. */
    unsigned saved = task_priority;
    task_priority = 12;
    assert(esp_display_present_engine_rebind_producer(m->engine, 1000) == ESP_ERR_INVALID_STATE);
    task_priority = saved;
    for (unsigned y = 0; y < 4; y += 2) {
        esp_display_present_lease_t lease;
        OK(esp_display_present_engine_acquire_buffer(m->engine, &lease));
        memset(lease.surface.pixels, 0xab, lease.surface.capacity_bytes);
        esp_display_present_engine_area_t area = {0, y, 3, y + 1};
        OK(esp_display_present_engine_submit_buffer(m->engine, &lease, &area, 8));
    }
    OK(esp_display_present_engine_commit_frame(m->engine, NULL));
    OK(esp_display_present_engine_drain(m->engine, 2000));
}
typedef struct {
    fixture_t *m;
    unsigned priority;
    bool control;
} handoff_t;
static void *handoff(void *u)
{
    handoff_t *h = u; task_priority = h->priority;
    OK(h->control ? present_presenter_control_bind_producer(h->m->engine, 2000) :
       esp_display_present_engine_rebind_producer(h->m->engine, 2000));
    frame(h->m);
    return NULL;
}
static void sessions(bool te, unsigned fixed)
{
    fixture_t m = {.te = te};
    atomic_store(&m.expected, 3); task_priority = 3;
    unsigned initial_updates = atomic_load(&copy_priority_updates);
    esp_display_present_engine_config_t cfg = {
        .backend_kind = te ? ESP_DISPLAY_PRESENT_ENGINE_TE : ESP_DISPLAY_PRESENT_ENGINE_FB,
        .mode = te ? ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC : ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL,
        .backend = {
            .width = 4, .height = 4, .tile_rows = 2, .buffer_count = 2, .max_frames = 4, .tile_count = 2, .compose_count = 2,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION,
            .async_copy = true, .copy_execute = execute, .copy_user = &m, .panel = {&m, attach, submit, quiesce}
        },
        .timeout_ms = 2000, .background_service = true, .worker_priority = fixed
    };
    if (te) cfg.backend.te_gate = (esp_display_present_te_gate_t) {
        .admit = admit
    };
    OK(esp_display_present_engine_create(&cfg, &m.engine));
    frame(&m);
    assert(atomic_load(&backend_priority) == (fixed ? fixed : 3));
    assert(atomic_load(&copy_priority_updates) == initial_updates + 1);
    const unsigned priorities[] = {9, 2, 6};
    for (unsigned i = 0; i < 3; i++) {
        atomic_store(&m.expected, priorities[i]);
        handoff_t h = {&m, priorities[i], i % 2 == 0}; pthread_t t;
        assert(!pthread_create(&t, NULL, handoff, &h)); assert(!pthread_join(t, NULL));
        assert(atomic_load(&backend_priority) == (fixed ? fixed : priorities[i]));
        assert(atomic_load(&copy_priority_updates) == initial_updates + i + 2);
    }
    /* Unchanged producer priority must not issue per-frame priority changes. */
    task_priority = 6; OK(esp_display_present_engine_rebind_producer(m.engine, 2000)); frame(&m);
    assert(atomic_load(&copy_priority_updates) == initial_updates + 4);
    assert(atomic_load(&m.submissions) == 5);
    assert(atomic_load(&m.copies) >= 10);
    OK(esp_display_present_engine_stop(m.engine, 2000)); OK(esp_display_present_engine_destroy(m.engine));
}
int main(void)
{
    for (unsigned te = 0; te < 2; te++) for (unsigned fixed = 0; fixed < 2; fixed++) {
            sessions(te, fixed ? 4 : 0);
        }
    puts("FB/TE split copy workers: producer-session priority handoff, fixed backend override, failed bind and no per-frame retuning passed");
}
