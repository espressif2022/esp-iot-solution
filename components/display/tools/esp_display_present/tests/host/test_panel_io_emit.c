/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "platform/lcd/present_lcd_io.h"
#include "platform/present_os.h"
#include "pixel/present_pixel_ops.h"
#include "core/present_engine.h"
#include "esp_cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *source, *expected, *seen;
    size_t stride, capacity, source_offset;
    unsigned w, h, bpp, rotation, out_w, out_h;
    bool swap;
} transform_t;
struct fake_lcd {
    esp_lcd_panel_io_color_trans_done_cb_t callback;
    void *user;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool early, pending, returned;
    const uint8_t *pixels;
    int x, y, ex, ey;
    unsigned draws, callbacks, completions;
    esp_err_t completion_result;
    esp_display_present_rect_t area;
    transform_t *transform;
};
static size_t allocations, max_allocation, live_allocations;
static bool internal_memory;
static esp_err_t cache_error;
static unsigned allocation_attempts, fail_allocation;
static void *allocated_slots[2];
static struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    bool enabled, entered, release, first_succeeds;
    unsigned calls;
} oom_gate = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
void *__real_present_alloc_pixels(size_t);
void *__wrap_present_alloc_pixels(size_t bytes)
{
    pthread_mutex_lock(&oom_gate.lock);
    bool deny = false;
    if (oom_gate.enabled) {
        ++oom_gate.calls;
        oom_gate.entered = true;
        pthread_cond_broadcast(&oom_gate.changed);
        while (!oom_gate.release) {
            pthread_cond_wait(&oom_gate.changed, &oom_gate.lock);
        }
        deny = !oom_gate.first_succeeds || oom_gate.calls > 1;
    }
    pthread_mutex_unlock(&oom_gate.lock);
    if (deny) {
        return NULL;
    }
    if (++allocation_attempts == fail_allocation) {
        return NULL;
    }
    assert(live_allocations < 2 && bytes <= 16384);
    void *p = __real_present_alloc_pixels(bytes);
    assert(p);
    allocated_slots[allocated_slots[0] ? 1 : 0] = p;
    allocations++;
    live_allocations++;
    if (bytes > max_allocation) {
        max_allocation = bytes;
    }
    return p;
}
void __real_present_free(void *);
void __wrap_present_free(void *p)
{
    for (size_t i = 0; i < 2; ++i) {
        if (p && allocated_slots[i] == p) {
            assert(live_allocations);
            live_allocations--;
            allocated_slots[i] = NULL;
        }
    }
    __real_present_free(p);
}
bool esp_ptr_internal(const void *p)
{
    (void)p;
    return internal_memory;
}
bool esp_ptr_dma_capable(const void *p)
{
    return p != NULL;
}
bool esp_ptr_dma_ext_capable(const void *p)
{
    return p != NULL;
}
esp_err_t esp_cache_msync(void *p, size_t bytes, int flags)
{
    assert(p && bytes && flags == (ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED));
    return cache_error;
}
esp_err_t esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,
                                                    const esp_lcd_panel_io_callbacks_t *cb, void *user)
{
    io->callback = cb->on_color_trans_done;
    io->user = user;
    return ESP_OK;
}
static void check_band(struct fake_lcd *p)
{
    transform_t *t = p->transform;
    assert(p->x == (int)p->area.x && p->ex == (int)(p->area.x + p->area.width));
    assert(p->y >= (int)p->area.y && p->ey <= (int)(p->area.y + p->area.height));
    size_t bytes = (size_t)(p->ex - p->x) * (p->ey - p->y) * t->bpp;
    assert(bytes && bytes <= 16384);
    size_t start = (size_t)(p->y - p->area.y) * t->out_w;
    assert(!memcmp(p->pixels, t->expected + start * t->bpp, bytes));
    for (size_t i = start; i < start + bytes / t->bpp; ++i) {
        assert(t->seen[i]++ == 0);
    }
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p,
                                    int x, int y, int ex, int ey, const void *pixels)
{
    pthread_mutex_lock(&p->lock);
    assert(!p->pending);
    p->pixels = pixels; p->x = x; p->y = y; p->ex = ex; p->ey = ey;
    p->pending = true;
    p->draws++;
    if (p->early) {
        check_band(p);
        p->pending = false;
        p->callbacks++;
    }
    pthread_cond_broadcast(&p->changed);
    pthread_mutex_unlock(&p->lock);
    if (p->early) {
        (void)p->callback(p, NULL, p->user);
    }
    return ESP_OK;
}
static bool completed(void *user, const esp_display_present_backend_event_t *event, bool isr)
{
    (void)isr;
    struct fake_lcd *p = user;
    assert(event->kind == ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE && (event->transaction_cookie == 91 || event->transaction_cookie == 92 || event->transaction_cookie == 93));
    p->completions++;
    p->completion_result = event->result;
    return false;
}
typedef struct {
    struct fake_lcd *panel;
    esp_display_present_panel_adapter_t api;
    esp_display_present_surface_t view;
    esp_display_present_engine_rotation_t rotation;
    esp_display_present_tx_result_t result;
} submission_t;
static void *submit(void *user)
{
    submission_t *s = user;
    s->result = s->api.emit(s->api.context, &s->view, &s->panel->area, s->rotation, 91);
    pthread_mutex_lock(&s->panel->lock);
    s->panel->returned = true;
    pthread_cond_broadcast(&s->panel->changed);
    pthread_mutex_unlock(&s->panel->lock);
    return NULL;
}
static void exercise(unsigned w, unsigned h, unsigned bpp, unsigned rot, bool swap,
                     unsigned padding, bool early)
{
    allocations = max_allocation = live_allocations = 0;
    transform_t t = {.w = w, .h = h, .bpp = bpp, .rotation = rot, .swap = swap};
    t.out_w = rot % 2 ? h : w; t.out_h = rot % 2 ? w : h;
    t.stride = (size_t)(w + (padding ? 8 : 0)) * bpp + padding;
    t.source_offset = padding ? 5 * t.stride + 3 * bpp : 0;
    t.capacity = t.stride * (h + (padding ? 9 : 0));
    assert(!posix_memalign((void **)&t.source, 64, t.capacity));
    memset(t.source, 0xDA, t.capacity);
    uint8_t *saved = malloc(t.capacity);
    t.expected = malloc((size_t)w * h * bpp); t.seen = calloc((size_t)w * h, 1);
    assert(saved && t.expected && t.seen);
    /* Independent forward-map oracle; source offsets are nonzero in a larger logical screen. */
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            uint8_t value[3] = {(uint8_t)((x + 7) * 13 + y), (uint8_t)((y + 11) * 17 + x), (uint8_t)(x ^ y ^ 0x65)};
            memcpy(t.source + t.source_offset + y * t.stride + x * bpp, value, bpp);
            unsigned dx, dy;
            switch (rot) {
            case 0: dx = x; dy = y; break;
            case 1: dx = h - 1 - y; dy = x; break;
            case 2: dx = w - 1 - x; dy = h - 1 - y; break;
            default: dx = y; dy = w - 1 - x; break;
            }
            for (unsigned b = 0; b < bpp; ++b) {
                t.expected[((size_t)dy * t.out_w + dx)*bpp + b] = value[swap ? 1 - b : b];
            }
        }
    memcpy(saved, t.source, t.capacity);
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER, .early = early, .transform = &t};
    esp_display_present_rect_t logical = {7, 11, w, h};
    assert(esp_display_present_rotate_rect(w + 29, h + 37, rot, &logical, &panel.area) == ESP_OK);
    esp_display_present_lcd_io_config_t cfg = {
        .panel = &panel, .io = &panel, .max_inflight = 1, .staging_bytes = 16384, .staging_timeout_ms = 2000,
        .format = bpp == 2 ? ESP_DISPLAY_PRESENT_FORMAT_RGB565 : ESP_DISPLAY_PRESENT_FORMAT_RGB888,
        .byte_order = swap ? ESP_DISPLAY_PRESENT_ORDER_SWAPPED : ESP_DISPLAY_PRESENT_ORDER_NATIVE,
        .region_mode = true,
    };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&cfg, &adapter) == ESP_OK);
    submission_t sub = {.panel = &panel};
    assert(esp_display_present_lcd_io_get_interface(adapter, &sub.api) == ESP_OK);
    assert(sub.api.attach(sub.api.context, completed, &panel) == ESP_OK);
    sub.rotation = rot;
    sub.view = (esp_display_present_surface_t) {
        t.source + t.source_offset, t.capacity - t.source_offset,
        {w, h, t.stride, cfg.format, ESP_DISPLAY_PRESENT_ORDER_NATIVE}
    };
    if (early) {
        submit(&sub);
    } else {
        pthread_t worker; assert(!pthread_create(&worker, NULL, submit, &sub));
        pthread_mutex_lock(&panel.lock);
        while (!panel.returned) {
            while (!panel.pending && !panel.returned) {
                pthread_cond_wait(&panel.changed, &panel.lock);
            }
            if (panel.pending) {
                check_band(&panel); panel.pending = false; panel.callbacks++;
                pthread_mutex_unlock(&panel.lock);
                (void)panel.callback(&panel, NULL, panel.user);
                pthread_mutex_lock(&panel.lock);
            }
        }
        pthread_mutex_unlock(&panel.lock);
        assert(!pthread_join(worker, NULL));
    }
    assert(!memcmp(saved, t.source, t.capacity));
    assert(allocations == 2 && live_allocations == 2 && max_allocation <= 16384);
    assert(sub.result.acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED && sub.result.result == ESP_OK);
    assert(panel.completions == 1 && panel.completion_result == ESP_OK);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        assert(t.seen[i] == 1);
    }
    assert(panel.draws == panel.callbacks);
    assert(sub.api.quiesce(sub.api.context, 100) == ESP_OK);
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && live_allocations == 0);
    pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
    free(saved); free(t.source); free(t.expected); free(t.seen);
}
static void overwide_row(void)
{
    allocations = max_allocation = live_allocations = 0;
    transform_t t = {0};
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER, .early = true, .transform = &t};
    esp_display_present_lcd_io_config_t cfg = {.panel = &panel, .io = &panel, .max_inflight = 1, .staging_bytes = 16384,
                                               .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .byte_order = ESP_DISPLAY_PRESENT_ORDER_NATIVE, .region_mode = true
                                              };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&cfg, &adapter) == ESP_OK);
    esp_display_present_panel_adapter_t api;
    assert(esp_display_present_lcd_io_get_interface(adapter, &api) == ESP_OK);
    assert(api.attach(api.context, completed, &panel) == ESP_OK);
    uint8_t *source;
    assert(!posix_memalign((void **)&source, 64, 18000));
    esp_display_present_surface_t surface = {source, 18000, {9000, 1, 18000, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}};
    esp_display_present_rect_t area = {0, 0, 9000, 1};
    esp_display_present_tx_result_t result = api.emit(api.context, &surface, &area, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 91);
    assert(result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED && result.result == ESP_ERR_INVALID_SIZE);
    assert(!panel.draws && !panel.completions);
    assert(api.quiesce(api.context, 100) == ESP_OK);
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && !live_allocations);
    free(source); pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
}
/* Direct swap, actual PSRAM DMA length/address fallback, and lazy OOM retry. */
static void routing_and_retry(void)
{
    for (unsigned path = 0; path < 7; ++path) {
        allocations = max_allocation = live_allocations = allocation_attempts = 0;
        internal_memory = path == 0 || path == 6;
        bool direct = path == 0 || path == 1 || path == 6;
        unsigned width = path == 2 ? 31 : 32;
        _Alignas(64) uint8_t storage[128], saved[64], expected[64], seen[32] = {0};
        uint8_t *source = storage + (path == 3 ? 2 : 0);
        for (unsigned i = 0; i < width * 2; ++i) {
            source[i] = saved[i] = (uint8_t)i;
            expected[i] = (uint8_t)(i ^ 1);
        }
        transform_t t = {.expected = expected, .seen = seen, .out_w = width, .bpp = 2};
        struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER,
                   .early = true, .transform = &t, .area = {0, 0, width, 1}
        };
        esp_display_present_lcd_io_config_t cfg = {.panel = &panel, .io = &panel, .max_inflight = 2, .staging_bytes = 16384,
                                                   .region_mode = true, .psram_dma_direct = path != 4 && path != 5, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                                   .byte_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED
                                                  };
        esp_display_present_lcd_io_t *adapter;
        assert(esp_display_present_lcd_io_create(&cfg, &adapter) == ESP_OK);
        esp_display_present_panel_adapter_t api;
        assert(esp_display_present_lcd_io_get_interface(adapter, &api) == ESP_OK);
        assert(api.attach(api.context, completed, &panel) == ESP_OK);
        esp_display_present_surface_t view = {source, width * 2, {width, 1, width * 2, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}};
        if (path == 5 || path == 6) {
            fail_allocation = path == 5 ? allocation_attempts + 2 : 0;
            cache_error = path == 6 ? ESP_FAIL : ESP_OK;
            esp_display_present_tx_result_t rejected = api.emit(api.context, &view, &panel.area, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 91);
            assert(rejected.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED);
            assert(!panel.draws && !panel.completions && !live_allocations);
            assert(!memcmp(source, saved, width * 2));
            fail_allocation = 0; cache_error = ESP_OK;
        }
        esp_display_present_tx_result_t result = api.emit(api.context, &view, &panel.area, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 91);
        assert(result.acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED && result.result == ESP_OK);
        assert(panel.completions == 1 && panel.draws == 1);
        assert(!memcmp(source, direct ? expected : saved, width * 2));
        assert(live_allocations == (direct ? 0u : 2u));
        assert(api.quiesce(api.context, 100) == ESP_OK);
        assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && !live_allocations);
        pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
    }
    internal_memory = false;
}

/* One adapter may alternate direct and staged requests. A staged request
 * waits for earlier direct DMA; rejection must not mutate its native pixels. */
static void mixed_requests(void)
{
    allocations = max_allocation = live_allocations = allocation_attempts = 0;
    internal_memory = false;
    _Alignas(64) uint8_t first[64], second[64], expected[64], seen[32] = {0};
    for (unsigned i = 0; i < 64; ++i) {
        first[i] = second[i] = (uint8_t)i;
        expected[i] = (uint8_t)(i ^ 1);
    }
    transform_t t = {.expected = expected, .seen = seen, .out_w = 32, .bpp = 2};
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER,
               .transform = &t, .area = {0, 0, 32, 1}
    };
    esp_display_present_lcd_io_config_t cfg = {.panel = &panel, .io = &panel, .max_inflight = 2, .staging_bytes = 16384,
                                               .region_mode = true, .psram_dma_direct = true, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                               .byte_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED
                                              };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&cfg, &adapter) == ESP_OK);
    esp_display_present_panel_adapter_t api;
    assert(esp_display_present_lcd_io_get_interface(adapter, &api) == ESP_OK);
    assert(api.attach(api.context, completed, &panel) == ESP_OK);
    esp_display_present_surface_t direct = {first, 64, {32, 1, 64, cfg.format, ESP_DISPLAY_PRESENT_ORDER_NATIVE}};
    esp_display_present_surface_t staged = {second, 62, {31, 1, 62, cfg.format, ESP_DISPLAY_PRESENT_ORDER_NATIVE}};
    esp_display_present_rect_t small = {0, 0, 31, 1};
    assert(api.emit(api.context, &direct, &panel.area, 0, 91).acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED);
    assert(!allocations && panel.pending && !memcmp(first, expected, 64));
    assert(api.emit(api.context, &staged, &small, 0, 92).acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED);
    assert(!allocations && second[0] == 0 && second[1] == 1);
    check_band(&panel); panel.pending = false;
    (void)panel.callback(&panel, NULL, panel.user);
    panel.area = small; t.out_w = 31; memset(seen, 0, sizeof(seen)); panel.early = true;
    assert(api.emit(api.context, &staged, &small, 0, 92).acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED);
    assert(live_allocations == 2 && second[0] == 0 && second[1] == 1);
    panel.area.width = t.out_w = 32; memset(seen, 0, sizeof(seen));
    for (unsigned i = 0; i < 64; ++i) {
        first[i] = (uint8_t)i;
    }
    assert(api.emit(api.context, &direct, &panel.area, 0, 93).acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED);
    assert(allocations == 2 && panel.completions == 3 && !memcmp(first, expected, 64));
    assert(api.quiesce(api.context, 100) == ESP_OK);
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && !live_allocations);
    pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
}

/* Real engine/backend, two full-screen PSRAM D and one output worker. While
 * D0 is awaiting DMA, the producer may draw D1, but cannot reuse D0. */
static void worker_full_screen(unsigned rotation)
{
    allocations = max_allocation = live_allocations = allocation_attempts = 0;
    const unsigned w = 480, h = 480;
    transform_t t = {.w = w, .h = h, .bpp = 2, .rotation = rotation, .swap = true, .out_w = w, .out_h = h};
    t.expected = malloc((size_t)w * h * 2); t.seen = calloc((size_t)w * h, 1);
    assert(t.expected && t.seen);
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER,
               .transform = &t, .area = {0, 0, w, h}
    };
    esp_display_present_lcd_io_config_t io = {.panel = &panel, .io = &panel, .max_inflight = 2, .staging_bytes = 16384,
                                              .staging_timeout_ms = 2000, .region_mode = true, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                              .byte_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED
                                             };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&io, &adapter) == ESP_OK);
    esp_display_present_engine_config_t cfg = {.backend = {.width = w, .height = h, .tile_rows = h,
                                                           .buffer_count = 2, .max_frames = 4, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                                           .panel_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED, .tile_in_psram = true,
                                                           .rotation = rotation, .deferred_submit = true
                                                          }, .background_service = true, .timeout_ms = 1000
                                              };
    assert(esp_display_present_lcd_io_get_interface(adapter, &cfg.backend.panel) == ESP_OK);
    esp_display_present_engine_t *engine;
    assert(esp_display_present_engine_create(&cfg, &engine) == ESP_OK);
    assert(!allocations); /* Neither X nor S at create. */
    esp_display_present_frame_request_t redraw = {.full_redraw = true};
    assert(esp_display_present_engine_begin_next_frame(engine, &redraw) == ESP_OK);
    esp_display_present_lease_t first, second;
    assert(esp_display_present_engine_acquire_buffer(engine, &first) == ESP_OK);
    t.source = first.surface.pixels;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            unsigned dx = x, dy = y;
            if (rotation == 1) {
                dx = h - 1 - y;
                dy = x;
            } else if (rotation == 2) {
                dx = w - 1 - x;
                dy = h - 1 - y;
            } else if (rotation == 3) {
                dx = y;
                dy = w - 1 - x;
            }
            uint8_t *pixel = t.source + ((size_t)y * w + x) * 2;
            pixel[0] = (uint8_t)(x * 7 + y); pixel[1] = (uint8_t)(y * 13 + x);
            t.expected[((size_t)dy * w + dx) * 2] = pixel[1]; t.expected[((size_t)dy * w + dx) * 2 + 1] = pixel[0];
        }
    esp_display_present_engine_area_t area = {0, 0, w - 1, h - 1};
    assert(esp_display_present_engine_submit_buffer(engine, &first, &area, w * 2) == ESP_OK);
    pthread_mutex_lock(&panel.lock);
    while (!panel.pending) {
        pthread_cond_wait(&panel.changed, &panel.lock);
    }
    pthread_mutex_unlock(&panel.lock);
    assert(esp_display_present_engine_acquire_buffer(engine, &second) == ESP_OK);
    assert(second.surface.pixels != first.surface.pixels);
    memset(second.surface.pixels, 0x5a, second.surface.capacity_bytes);
    unsigned expected_bands = (h + 16) / 17; /* 16384/(480*2) = 17 rows. */
    for (unsigned i = 0; i < expected_bands; ++i) {
        pthread_mutex_lock(&panel.lock);
        while (!panel.pending) {
            pthread_cond_wait(&panel.changed, &panel.lock);
        }
        check_band(&panel); panel.pending = false; panel.callbacks++;
        pthread_mutex_unlock(&panel.lock);
        (void)panel.callback(&panel, NULL, panel.user);
    }
    /* Cancels only the producer's unsubmitted D1, then joins the output task. */
    assert(esp_display_present_engine_cancel_frame(engine) == ESP_OK);
    assert(esp_display_present_engine_stop(engine, 1000) == ESP_OK);
    assert(esp_display_present_engine_destroy(engine) == ESP_OK);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        assert(t.seen[i] == 1);
    }
    assert(panel.draws == expected_bands && live_allocations == 2);
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && !live_allocations);
    free(t.expected); free(t.seen);
    pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
}

/* Persistent failure must terminate accepted work without another allocation
 * attempt, including when the transfer deadline is disabled. Gate allocation
 * until the frame is sealed so failure/commit ordering is deterministic. */
static void worker_staging_oom(bool first_succeeds, uint32_t transfer_timeout)
{
    allocations = max_allocation = live_allocations = allocation_attempts = 0;
    internal_memory = false;
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    esp_display_present_lcd_io_config_t io = {.panel = &panel, .io = &panel, .max_inflight = 1,
                                              .staging_bytes = 64, .region_mode = true, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565
                                             };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&io, &adapter) == ESP_OK);
    esp_display_present_engine_config_t cfg = {.backend = {.width = 4, .height = 4, .tile_rows = 4,
                                                           .buffer_count = 1, .max_frames = 2, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                                           .deferred_submit = true
                                                          }, .background_service = true, .timeout_ms = 1000,
                                               .transfer_timeout_ms = transfer_timeout
                                              };
    assert(esp_display_present_lcd_io_get_interface(adapter, &cfg.backend.panel) == ESP_OK);
    esp_display_present_engine_t *engine;
    assert(esp_display_present_engine_create(&cfg, &engine) == ESP_OK);
    esp_display_present_frame_request_t request = {.full_redraw = true};
    assert(esp_display_present_engine_begin_next_frame(engine, &request) == ESP_OK);
    esp_display_present_lease_t lease;
    assert(esp_display_present_engine_acquire_buffer(engine, &lease) == ESP_OK);
    memset(lease.surface.pixels, 0x21, 32);
    pthread_mutex_lock(&oom_gate.lock);
    oom_gate.enabled = true; oom_gate.entered = oom_gate.release = false;
    oom_gate.first_succeeds = first_succeeds; oom_gate.calls = 0;
    pthread_mutex_unlock(&oom_gate.lock);
    esp_display_present_engine_area_t area = {0, 0, 3, 3};
    assert(esp_display_present_engine_submit_buffer(engine, &lease, &area, 8) == ESP_OK);
    pthread_mutex_lock(&oom_gate.lock);
    while (!oom_gate.entered) {
        pthread_cond_wait(&oom_gate.changed, &oom_gate.lock);
    }
    pthread_mutex_unlock(&oom_gate.lock);
    esp_display_present_engine_receipt_t receipt;
    assert(esp_display_present_engine_commit_with_receipt(engine, NULL, &receipt) == ESP_OK);
    pthread_mutex_lock(&oom_gate.lock);
    oom_gate.release = true; pthread_cond_broadcast(&oom_gate.changed);
    pthread_mutex_unlock(&oom_gate.lock);
    esp_display_present_frame_status_t status;
    assert(esp_display_present_engine_wait_frame(engine, receipt, ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED, 1000, &status) == ESP_OK);
    assert(status.result == ESP_ERR_NO_MEM && status.terminal && status.src_released);
    assert(esp_display_present_engine_release_receipt(engine, receipt) == ESP_OK);
    assert(esp_display_present_engine_stop(engine, 1000) == ESP_OK);
    assert(esp_display_present_engine_destroy(engine) == ESP_OK);
    assert(oom_gate.calls == 2 && !panel.draws && !live_allocations);
    assert(allocations == (first_succeeds ? 1u : 0u));
    oom_gate.enabled = false;
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK);
    pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
}

static esp_display_present_tx_result_t (*real_emit)(void *, const esp_display_present_surface_t *,
                                                    const esp_display_present_rect_t *, esp_display_present_engine_rotation_t, uint64_t);
static atomic_uint busy_attempts;
static present_os_event_t *busy_seen;
static esp_display_present_tx_result_t count_busy_emit(void *context, const esp_display_present_surface_t *surface,
                                                       const esp_display_present_rect_t *area, esp_display_present_engine_rotation_t rotation, uint64_t cookie)
{
    esp_display_present_tx_result_t result = real_emit(context, surface, area, rotation, cookie);
    if (result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED && result.result == ESP_ERR_NOT_FINISHED) {
        atomic_fetch_add(&busy_attempts, 1);
        present_event_signal(busy_seen, false);
    }
    return result;
}

/* A direct DMA keeps the S pair unavailable. Repeated service must wait for
 * its real completion instead of scheduling itself in a tight loop. */
static void worker_backpressure(void)
{
    allocations = max_allocation = live_allocations = allocation_attempts = 0;
    internal_memory = false;
    uint8_t expected[64], seen[32] = {0}; memset(expected, 0x21, sizeof(expected));
    transform_t transform = {.expected = expected, .seen = seen, .out_w = 32, .bpp = 2};
    struct fake_lcd panel = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER,
               .transform = &transform, .area = {0, 0, 32, 1}
    };
    esp_display_present_lcd_io_config_t io = {.panel = &panel, .io = &panel, .max_inflight = 2,
                                              .staging_bytes = 64, .region_mode = true, .psram_dma_direct = true, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565
                                             };
    esp_display_present_lcd_io_t *adapter;
    assert(esp_display_present_lcd_io_create(&io, &adapter) == ESP_OK);
    esp_display_present_engine_config_t cfg = {.backend = {.width = 32, .height = 2, .tile_rows = 1,
                                                           .buffer_count = 2, .max_frames = 2, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
                                                           .deferred_submit = true
                                                          }, .background_service = true, .timeout_ms = 1000, .transfer_timeout_ms = 1000
                                              };
    assert(esp_display_present_lcd_io_get_interface(adapter, &cfg.backend.panel) == ESP_OK);
    real_emit = cfg.backend.panel.emit; cfg.backend.panel.emit = count_busy_emit;
    busy_seen = present_event_create(); assert(busy_seen); atomic_store(&busy_attempts, 0);
    esp_display_present_engine_t *engine;
    assert(esp_display_present_engine_create(&cfg, &engine) == ESP_OK);
    esp_display_present_frame_request_t request = {.full_redraw = true};
    assert(esp_display_present_engine_begin_next_frame(engine, &request) == ESP_OK);
    esp_display_present_lease_t first, second;
    assert(esp_display_present_engine_acquire_buffer(engine, &first) == ESP_OK);
    memset(first.surface.pixels, 0x21, 64);
    esp_display_present_engine_area_t area = {0, 0, 31, 0};
    assert(esp_display_present_engine_submit_buffer(engine, &first, &area, 64) == ESP_OK);
    pthread_mutex_lock(&panel.lock);
    while (!panel.pending) {
        pthread_cond_wait(&panel.changed, &panel.lock);
    }
    pthread_mutex_unlock(&panel.lock);
    assert(esp_display_present_engine_acquire_buffer(engine, &second) == ESP_OK);
    memset(second.surface.pixels, 0x21, 64);
    area = (esp_display_present_engine_area_t) {
        0, 1, 30, 1
    };
    assert(esp_display_present_engine_submit_buffer(engine, &second, &area, 62) == ESP_OK);
    esp_display_present_engine_receipt_t receipt;
    assert(esp_display_present_engine_commit_with_receipt(engine, NULL, &receipt) == ESP_OK);
    assert(present_event_wait(busy_seen, 1000));
    /* Use an event timeout as a bounded observation window, not a success race. */
    present_os_event_t *quiet = present_event_create(); assert(quiet);
    assert(!present_event_wait(quiet, 20)); present_event_destroy(quiet);
    assert(atomic_load(&busy_attempts) <= 3);
    pthread_mutex_lock(&panel.lock);
    assert(panel.draws == 1 && panel.pending && !allocations);
    check_band(&panel); panel.pending = false; ++panel.callbacks;
    panel.area = (esp_display_present_rect_t) {
        0, 1, 31, 1
    }; transform.out_w = 31;
    memset(seen, 0, sizeof(seen)); panel.early = true;
    pthread_mutex_unlock(&panel.lock);
    (void)panel.callback(&panel, NULL, panel.user);
    esp_display_present_frame_status_t status;
    assert(esp_display_present_engine_wait_frame(engine, receipt, ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED, 1000, &status) == ESP_OK);
    assert(status.result == ESP_OK && status.terminal && status.src_released);
    assert(esp_display_present_engine_release_receipt(engine, receipt) == ESP_OK);
    assert(esp_display_present_engine_stop(engine, 1000) == ESP_OK);
    assert(esp_display_present_engine_destroy(engine) == ESP_OK);
    assert(panel.draws == 2 && live_allocations == 2);
    assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK && !live_allocations);
    present_event_destroy(busy_seen);
    pthread_mutex_destroy(&panel.lock); pthread_cond_destroy(&panel.changed);
}

int main(void)
{
    const unsigned shapes[][2] = {{480, 480}, {101, 83}, {320, 241}, {17, 19}, {32, 257}, {1, 1}};
    unsigned cases = 0;
    for (unsigned shape = 0; shape < 6; ++shape) for (unsigned rot = 0; rot < 4; ++rot)
            for (unsigned format = 0; format < 3; ++format) for (unsigned pad = 0; pad < 2; ++pad)
                    for (unsigned early = 0; early < 2; ++early) {
                        exercise(shapes[shape][0], shapes[shape][1], format == 2 ? 3 : 2, rot, format == 1, pad ? 4 : 0, early);
                        ++cases;
                    }
    for (unsigned first = 0; first < 2; ++first) {
        worker_staging_oom(first, 0);
        worker_staging_oom(first, 10);
    }
    worker_backpressure();
    overwide_row();
    routing_and_retry();
    mixed_requests();
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        worker_full_screen(rotation);
    }
    printf("PASS: %u wire-image cases; all rotations, RGB565/swap/RGB888, padded/packed source, offset rectangles, early/delayed callbacks, tails; 2x<=16KiB allocations, immutable source; overwide-row preflight rejection\n", cases);
    return 0;
}
