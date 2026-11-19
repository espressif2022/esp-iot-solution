/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "backend/present_backend.h"
#include "backend/present_backend_config.h"
#include "core/present_engine.h"
#include "core/present_control.h"
#include "pixel/present_pixel_ops.h"
#include "platform/present_limits.h"
#include "platform/present_os.h"
#include "scan_panel.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define OK(x) assert((x) == ESP_OK)
static const present_backend_ops_t *ops = &present_fb_backend;

static void write_rect(esp_display_present_lease_t *lease, esp_display_present_rect_t a, uint16_t value)
{
    for (uint32_t y = a.y; y < a.y + a.height; ++y)
        for (uint32_t x = a.x; x < a.x + a.width; ++x) {
            memcpy((uint8_t *)lease->surface.pixels + y * lease->surface.layout.stride_bytes + 2 * x, &value, 2);
        }
}
static void send_rect(present_backend_t *b, esp_display_present_frame_t f,
                      esp_display_present_rect_t a, uint16_t value)
{
    esp_display_present_lease_t lease;
    OK(ops->acquire(b, f, &a, 0, &lease));
    write_rect(&lease, a, value);
    esp_display_present_region_submit_t s = {.logical_area = a, .stride_bytes = lease.surface.layout.stride_bytes};
    OK(ops->submit_region_ex(b, f, lease.id, &s));
}
static void drain_build(present_backend_t *b, scan_panel_t *panel)
{
    uint64_t start = present_now_ms();
    while (!panel->cookie) {
        (void)ops->copy_work(b);
        OK(ops->service(b));
        assert(!present_expired(start, 1000));
    }
}
static void finish(present_backend_t *b, scan_panel_t *p, esp_display_present_receipt_t receipt)
{
    drain_build(b, p);
    scan_confirm(p);
    esp_display_present_frame_status_t status;
    OK(ops->wait(b, receipt, ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED, 0, &status));
    assert(status.terminal && status.result == ESP_OK);
    OK(ops->release_receipt(b, receipt));
}
static void bounded_canvas(unsigned rotation)
{
    scan_panel_t p = {0};
    esp_display_present_backend_config_t cfg = {
        .width = 70, .height = 4, .buffer_count = 3, .max_frames = 4,
        .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .rotation = rotation,
        .render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT,
        .async_copy = true, .copy_on_backend_worker = true, .panel = scan_interface(&p)
    };
    present_backend_t *b;
    OK(ops->create(&cfg, &b));
    esp_display_present_frame_request_t req = {.resolved_pixels = true};
    esp_display_present_frame_t f;
    OK(ops->begin(b, &req, &f));
    esp_display_present_rect_t top = {0, 0, 70, 2}, bottom = {0, 2, 70, 2};
    send_rect(b, f, top, 0x1111);
    esp_display_present_lease_t lease;
    assert(ops->acquire(b, f, NULL, 0, &lease) == ESP_ERR_TIMEOUT);
    assert(ops->acquire(b, f, &top, 0, &lease) == ESP_ERR_TIMEOUT);
    send_rect(b, f, bottom, 0x1111); /* Disjoint region succeeds with both jobs still queued. */
    assert(ops->can_defer_service(b));
    assert(!p.submissions);
    esp_display_present_receipt_t receipt;
    OK(ops->end(b, f, &receipt));
    assert(!ops->can_defer_service(b));
    drain_build(b, &p);
    assert(p.submissions == 1);
    /* Keep a prior frame pending while constructing another. */
    OK(ops->begin(b, &req, &f));
    assert(ops->can_defer_service(b));
    scan_confirm(&p);
    assert(!ops->can_defer_service(b)); /* Real retirement must not be skipped. */
    OK(ops->service(b));
    assert(ops->can_defer_service(b));
    esp_display_present_frame_status_t status;
    OK(ops->query(b, receipt, &status));
    OK(ops->release_receipt(b, receipt));
    for (unsigned i = 0; i < PRESENT_MAX_DAMAGE_AREAS; ++i) {
        send_rect(b, f, (esp_display_present_rect_t) {
            i, 0, 1, 1
        }, 0x2222);
    }
    esp_display_present_rect_t extra = {PRESENT_MAX_DAMAGE_AREAS, 0, 1, 1};
    OK(ops->acquire(b, f, &extra, 0, &lease));
    write_rect(&lease, extra, 0x3333);
    esp_display_present_region_submit_t s = {.logical_area = extra, .stride_bytes = lease.surface.layout.stride_bytes};
    assert(ops->submit_region_ex(b, f, lease.id, &s) == ESP_ERR_NO_MEM);
    assert(ops->copy_work(b));
    OK(ops->submit_region_ex(b, f, lease.id, &s)); /* Completed descriptor recycled by retry. */
    /* First region's real completion permits an overlapping new write.
     * Free a descriptor as well: the bounded queue is still full. */
    assert(ops->copy_work(b));
    send_rect(b, f, (esp_display_present_rect_t) {
        0, 0, 1, 1
    }, 0x4444);
    OK(ops->end(b, f, &receipt));
    finish(b, &p, receipt);
    for (unsigned y = 0; y < 4; ++y) {
        for (unsigned x = 0; x < 70; ++x) {
            unsigned px = x, py = y, pw = 70;
            if (rotation == 1) {
                px = 3 - y;
                py = x;
                pw = 4;
            }
            if (rotation == 2) {
                px = 69 - x;
                py = 3 - y;
            }
            if (rotation == 3) {
                px = y;
                py = 69 - x;
                pw = 4;
            }
            uint16_t actual, expected = 0x1111;
            if (!y && x < PRESENT_MAX_DAMAGE_AREAS) {
                expected = x ? 0x2222 : 0x4444;
            }
            if (!y && x == PRESENT_MAX_DAMAGE_AREAS) {
                expected = 0x3333;
            }
            memcpy(&actual, (uint8_t *)p.current.pixels + (py * pw + px) * 2, 2);
            assert(actual == expected);
        }
    }
    OK(ops->begin(b, &req, &f));
    send_rect(b, f, top, 0x7777);
    send_rect(b, f, bottom, 0x7777);
    assert(ops->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
    assert(ops->copy_work(b));
    assert(ops->cancel_frame(b, f, 0) == ESP_ERR_TIMEOUT);
    assert(ops->copy_work(b));
    OK(ops->cancel_frame(b, f, 0));
    assert(p.submissions == 2);
    OK(ops->stop(b, 1000));
    OK(ops->destroy(b));
}

/* Host records static priority selection and lock ordering; it does not
 * simulate FreeRTOS scheduling or establish hardware timing/FPS. */
static _Thread_local unsigned gate_depth;
static _Thread_local bool taking_mutex;
static pthread_t owner_task;
static atomic_bool owner_bound, check_work_signals;
static _Atomic(present_os_event_t *) work_event;
bool __real_present_event_signal(present_os_event_t *, bool);
static atomic_uint last_priority, priority_changes;
static _Thread_local unsigned task_priority = 6;
static atomic_bool observe_worker;
static pthread_t worker_task;
static atomic_uint drawing_gate_takes;
static atomic_bool drawing_probe, worker_idle;
bool __real_present_mutex_take(present_mutex_t *, uint32_t);
void __real_present_mutex_give(present_mutex_t *);
void __real_present_worker_set_priority(present_worker_t *, unsigned);
present_worker_t *__real_present_worker_create(const char *, present_worker_entry_fn, void *, uint32_t, unsigned);
bool __real_present_event_wait(present_os_event_t *, uint32_t);
unsigned __wrap_present_task_priority(void)
{
    assert(!gate_depth);
    return task_priority;
}
bool __wrap_present_mutex_take(present_mutex_t *m, uint32_t timeout)
{
    if (atomic_load(&observe_worker) && pthread_equal(pthread_self(), worker_task) && atomic_load(&drawing_probe)) {
        atomic_fetch_add(&drawing_gate_takes, 1);
    }
    taking_mutex = true;
    bool result = __real_present_mutex_take(m, timeout);
    taking_mutex = false;
    if (result) {
        ++gate_depth;
    }
    return result;
}

void __wrap_present_mutex_give(present_mutex_t *m)
{
    assert(gate_depth);
    --gate_depth;
    __real_present_mutex_give(m);
}
present_worker_t *__wrap_present_worker_create(const char *name, present_worker_entry_fn entry, void *user,
                                               uint32_t stack, unsigned priority)
{
    if (!strcmp(name, "present_backend")) {
        atomic_store(&last_priority, priority);
    }
    return __real_present_worker_create(name, entry, user, stack, priority);
}
void __wrap_present_worker_set_priority(present_worker_t *w, unsigned p)
{
    assert(!gate_depth);
    atomic_store(&last_priority, p);
    atomic_fetch_add(&priority_changes, 1);
    __real_present_worker_set_priority(w, p);
}
bool __wrap_present_event_signal(present_os_event_t *event, bool isr)
{
    if (!isr && atomic_load(&check_work_signals) && event == atomic_load(&work_event) &&
            pthread_equal(pthread_self(), owner_task)) {
        assert(!gate_depth);
    }
    return __real_present_event_signal(event, isr);
}
bool __wrap_present_event_wait(present_os_event_t *event, uint32_t timeout)
{
    if (!taking_mutex && atomic_load(&owner_bound) && !pthread_equal(pthread_self(), owner_task)) {
        atomic_store(&work_event, event);
    }
    if (!taking_mutex && atomic_load(&observe_worker) && pthread_equal(pthread_self(), worker_task) && atomic_load(&drawing_probe)) {
        atomic_store(&worker_idle, true);
    }
    return __real_present_event_wait(event, timeout);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    esp_display_present_panel_event_fn callback;
    void *user;
    esp_display_present_surface_t surface;
    uint64_t cookie;
    unsigned copies, submissions;
    bool block, release, fail;
} model_t;
static esp_err_t attach(void *u, esp_display_present_panel_event_fn cb, void *ctx)
{
    model_t *m = u; m->callback = cb; m->user = ctx; return ESP_OK;
}
static esp_display_present_tx_result_t submit(void *u, const esp_display_present_surface_t *s,
                                              const esp_display_present_rect_t *a, uint64_t cookie)
{
    (void)a; model_t *m = u;
    pthread_mutex_lock(&m->lock);
    assert(!m->cookie); m->surface = *s; m->cookie = cookie; ++m->submissions;
    pthread_cond_broadcast(&m->changed); pthread_mutex_unlock(&m->lock);
    return (esp_display_present_tx_result_t) {
        ESP_DISPLAY_PRESENT_TX_ACCEPTED, ESP_OK
    };
}
static esp_err_t quiesce(void *u, uint32_t timeout)
{
    (void)timeout; model_t *m = u; pthread_mutex_lock(&m->lock);
    esp_err_t e = m->cookie ? ESP_ERR_TIMEOUT : ESP_OK;
    if (e == ESP_OK) {
        m->callback = NULL;
    }
    pthread_mutex_unlock(&m->lock); return e;
}
static esp_err_t execute(void *u, const esp_display_present_copy_request_t *r)
{
    model_t *m = u; assert(!gate_depth);
    pthread_mutex_lock(&m->lock);
    if (!m->copies) {
        worker_task = pthread_self(); atomic_store(&observe_worker, true);
        assert(atomic_load(&last_priority) == 6);
    }
    ++m->copies; pthread_cond_broadcast(&m->changed);
    while (m->block && !m->release) {
        pthread_cond_wait(&m->changed, &m->lock);
    }
    bool fail = m->fail; pthread_mutex_unlock(&m->lock);
    return fail ? ESP_FAIL : esp_display_present_pixel_execute_cpu(r);
}
static void await_count(model_t *m, bool submitted, unsigned count)
{
    struct timespec until; clock_gettime(CLOCK_MONOTONIC, &until); until.tv_sec += 2;
    pthread_mutex_lock(&m->lock);
    while ((submitted ? m->submissions : m->copies) < count) {
        assert(!pthread_cond_timedwait(&m->changed, &m->lock, &until));
    }
    pthread_mutex_unlock(&m->lock);
}
static esp_display_present_engine_t *create_variant(model_t *m, unsigned priority, bool io)
{
    owner_task = pthread_self();
    atomic_store(&owner_bound, true);
    atomic_store(&work_event, NULL);
    atomic_store(&check_work_signals, false);
    assert(!pthread_mutex_init(&m->lock, NULL));
    pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    assert(!pthread_cond_init(&m->changed, &a)); pthread_condattr_destroy(&a);
    atomic_store(&observe_worker, false); atomic_store(&drawing_probe, false);
    atomic_store(&worker_idle, false); atomic_store(&drawing_gate_takes, 0);
    atomic_store(&last_priority, 0); atomic_store(&priority_changes, 0);
    esp_display_present_engine_config_t cfg = {
        .backend_kind = ESP_DISPLAY_PRESENT_ENGINE_FB, .mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT,
        .backend = {
            .width = 4, .height = 4, .buffer_count = 2, .max_frames = 4,
            .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .rotation = ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90,
            .render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT,
            .async_copy = true, .copy_on_backend_worker = true, .copy_execute = execute, .copy_user = m,
            .panel = {m, attach, submit, quiesce}
        },
        .timeout_ms = 10, .worker_priority = priority, .background_service = true
    };
    if (io) {
        cfg.backend_kind = ESP_DISPLAY_PRESENT_ENGINE_REGION;
        cfg.mode = ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE;
        cfg.backend.rotation = ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0;
        cfg.backend.render_contract = ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION;
        cfg.backend.async_copy = cfg.backend.copy_on_backend_worker = false;
        cfg.backend.copy_execute = NULL;
        cfg.backend.copy_user = NULL;
        cfg.backend.deferred_submit = true;
        cfg.backend.tile_rows = 2;
        cfg.backend.transfer_capacity = 1;
    }
    esp_display_present_engine_t *p;
    OK(esp_display_present_engine_create(&cfg, &p));
    uint64_t start = present_now_ms();
    while (!atomic_load(&work_event)) {
        assert(!present_expired(start, 1000));
        present_pause();
    }
    atomic_store(&check_work_signals, true);
    return p;
}
static esp_display_present_engine_t *create_with_priority(model_t *m, unsigned priority)
{
    return create_variant(m, priority, false);
}
static esp_display_present_engine_t *create(model_t *m)
{
    return create_with_priority(m, 0);
}
static esp_display_present_lease_t region(esp_display_present_engine_t *p, unsigned y)
{
    esp_display_present_rect_t a = {0, y, 4, 2}; esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_region(p, &a, &lease)); write_rect(&lease, a, 0xabab);
    esp_display_present_engine_area_t s = {0, y, 3, y + 1};
    OK(esp_display_present_engine_submit_buffer(p, &lease, &s, lease.surface.layout.stride_bytes)); return lease;
}
static void free_model(esp_display_present_engine_t *p, model_t *m)
{
    atomic_store(&check_work_signals, false);
    OK(esp_display_present_engine_stop(p, 1000)); OK(esp_display_present_engine_destroy(p));
    atomic_store(&observe_worker, false);
    atomic_store(&owner_bound, false);
    pthread_cond_destroy(&m->changed); pthread_mutex_destroy(&m->lock);
}
static void release_copy(model_t *m)
{
    pthread_mutex_lock(&m->lock); m->release = true; pthread_cond_broadcast(&m->changed); pthread_mutex_unlock(&m->lock);
}
static void source_ownership_and_seal(bool cancel)
{
    model_t m = {.block = true}; esp_display_present_engine_t *p = create(&m);
    esp_display_present_frame_request_t req = {.resolved_pixels = true};
    OK(esp_display_present_engine_begin_next_frame(p, &req));
    esp_display_present_lease_t first = region(p, 0); await_count(&m, false, 1);
    (void)region(p, 2); assert(atomic_load(&last_priority) == 6);
    esp_display_present_rect_t overlap = {0, 0, 1, 1}; esp_display_present_lease_t lease;
    assert(esp_display_present_engine_acquire_region(p, &overlap, &lease) == ESP_ERR_TIMEOUT);
    assert(esp_display_present_engine_acquire_buffer(p, &lease) == ESP_ERR_TIMEOUT);
    esp_display_present_engine_status_t st; OK(esp_display_present_engine_get_status(p, &st)); assert(st.first_error == ESP_OK);
    if (cancel) {
        atomic_store(&check_work_signals, false);
        assert(esp_display_present_engine_cancel_frame(p) == ESP_ERR_TIMEOUT);
        assert(esp_display_present_engine_stop(p, 5) == ESP_ERR_TIMEOUT);
        assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
        assert(!m.submissions);
        for (unsigned x = 0; x < 16; ++x) {
            assert(((uint8_t *)first.surface.pixels)[x] == 0xab);
        }
        release_copy(&m); free_model(p, &m); assert(m.copies == 1 && !m.submissions);
    } else {
        OK(esp_display_present_engine_commit_frame(p, NULL));
        assert(atomic_load(&last_priority) == 6 && !atomic_load(&priority_changes));
        release_copy(&m); await_count(&m, true, 1); assert(m.copies == 2);
        pthread_mutex_lock(&m.lock);
        for (unsigned x = 0; x < 32; ++x) {
            assert(((uint8_t *)m.surface.pixels)[x] == 0xab);
        }
        esp_display_present_backend_event_t event = {.kind = ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED, .transaction_cookie = m.cookie};
        m.cookie = 0; (void)m.callback(m.user, &event, false); pthread_mutex_unlock(&m.lock);
        OK(esp_display_present_engine_drain(p, 1000)); free_model(p, &m);
    }
}
static void construction_does_not_pump(void)
{
    model_t m = {0}; esp_display_present_engine_t *p = create(&m);
    esp_display_present_frame_request_t req = {.resolved_pixels = true};
    OK(esp_display_present_engine_begin_next_frame(p, &req));
    atomic_store(&drawing_probe, true); (void)region(p, 0); await_count(&m, false, 1);
    uint64_t start = present_now_ms();
    while (!atomic_load(&worker_idle)) {
        assert(!present_expired(start, 1000));
        present_pause();
    }
    assert(!atomic_load(&drawing_gate_takes));
    atomic_store(&drawing_probe, false);
    OK(esp_display_present_engine_cancel_frame(p)); free_model(p, &m);
}
typedef struct {
    esp_display_present_engine_t *engine;
    unsigned priority;
} handoff_t;
static void *bind_from_task(void *user)
{
    handoff_t *handoff = user;
    task_priority = handoff->priority;
    OK(present_presenter_control_bind_producer(handoff->engine, 1000));
    return NULL;
}
static void static_priority_selection(void)
{
    const unsigned priorities[] = {3, 6, 9};
    for (unsigned fixed = 0; fixed < 2; ++fixed) {
        for (size_t i = 0; i < sizeof(priorities) / sizeof(priorities[0]); ++i) {
            task_priority = priorities[i];
            model_t m = {0};
            esp_display_present_engine_t *p = create_with_priority(&m, fixed ? 4 : 0);
            unsigned selected = fixed ? 4 : task_priority;
            assert(atomic_load(&last_priority) == selected && !atomic_load(&priority_changes));
            /* An application priority change does not retune an active session. */
            task_priority = priorities[(i + 1) % 3];
            esp_display_present_frame_request_t req = {0};
            OK(esp_display_present_engine_begin_next_frame(p, &req));
            assert(esp_display_present_engine_rebind_producer(p, 1000) == ESP_ERR_INVALID_STATE);
            assert(atomic_load(&last_priority) == selected && !atomic_load(&priority_changes));
            OK(esp_display_present_engine_cancel_frame(p));
            OK(esp_display_present_engine_rebind_producer(p, 1000));
            selected = fixed ? 4 : task_priority;
            assert(atomic_load(&last_priority) == selected);
            assert(atomic_load(&priority_changes) == (fixed ? 0u : 1u));
            /* LVGL/control handoff snapshots the actual new producer, not creator. */
            handoff_t handoff = {p, priorities[(i + 2) % 3]};
            pthread_t thread;
            assert(!pthread_create(&thread, NULL, bind_from_task, &handoff));
            assert(!pthread_join(thread, NULL));
            assert(atomic_load(&last_priority) == (fixed ? 4 : handoff.priority));
            assert(atomic_load(&priority_changes) == (fixed ? 0u : 2u));
            OK(esp_display_present_engine_rebind_producer(p, 1000));
            free_model(p, &m);
        }
    }
    task_priority = 6;
}

static void copy_failure_is_not_hidden(void)
{
    model_t m = {.fail = true}; esp_display_present_engine_t *p = create(&m);
    esp_display_present_frame_request_t req = {.resolved_pixels = true}; OK(esp_display_present_engine_begin_next_frame(p, &req));
    (void)region(p, 0); await_count(&m, false, 1);
    esp_display_present_engine_status_t st; uint64_t start = present_now_ms();
    do {
        OK(esp_display_present_engine_get_status(p, &st));
        assert(!present_expired(start, 1000));
        present_pause();
    } while (st.first_error == ESP_OK);
    assert(st.first_error == ESP_FAIL && !m.submissions); free_model(p, &m);
}
static void *complete_region(void *user)
{
    model_t *m = user;
    pthread_mutex_lock(&m->lock);
    assert(m->cookie);
    for (unsigned i = 0; i < 16; ++i) {
        assert(((const uint8_t *)m->surface.pixels)[i] == 0xab);
    }
    esp_display_present_backend_event_t event = {
        .kind = ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE, .transaction_cookie = m->cookie
    };
    m->cookie = 0;
    (void)m->callback(m->user, &event, false);
    pthread_mutex_unlock(&m->lock);
    return NULL;
}
static void submit_region_tile(esp_display_present_engine_t *p, unsigned y)
{
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(p, &lease));
    memset(lease.surface.pixels, 0xab, 16);
    esp_display_present_engine_area_t area = {0, y, 3, y + 1};
    OK(esp_display_present_engine_submit_buffer(p, &lease, &area, 8));
}
static void region_enqueue_notifications(void)
{
    for (unsigned stop = 0; stop < 2; ++stop) {
        model_t m = {0}; esp_display_present_engine_t *p = create_variant(&m, 9, true);
        esp_display_present_frame_request_t request = {0};
        OK(esp_display_present_engine_begin_next_frame(p, &request));
        submit_region_tile(p, 0); await_count(&m, true, 1);
        submit_region_tile(p, 2); /* Queued behind the first DMA source. */
        OK(esp_display_present_engine_commit_frame(p, NULL));
        if (stop) {
            atomic_store(&check_work_signals, false); /* STOP deliberately wakes immediately. */
            assert(esp_display_present_engine_stop(p, 0) == ESP_ERR_TIMEOUT);
            assert(esp_display_present_engine_destroy(p) == ESP_ERR_INVALID_STATE);
        }
        pthread_t completion;
        assert(!pthread_create(&completion, NULL, complete_region, &m));
        assert(!pthread_join(completion, NULL));
        if (!stop) {
            await_count(&m, true, 2); /* Real completion must wake queued output. */
            assert(!pthread_create(&completion, NULL, complete_region, &m));
            assert(!pthread_join(completion, NULL));
            OK(esp_display_present_engine_drain(p, 1000));
        }
        free_model(p, &m);
        assert(m.submissions == (stop ? 1u : 2u));
    }
}
int main(void)
{
    for (unsigned r = 1; r < 4; ++r) {
        bounded_canvas(r);
    }
    region_enqueue_notifications();
    static_priority_selection();
    construction_does_not_pump(); source_ownership_and_seal(false); source_ownership_and_seal(true); copy_failure_is_not_hidden();
    puts("Dependency scheduling: disjoint overlap, bounded descriptors, retirement, static session priorities, cancellation and late completion passed");
}
