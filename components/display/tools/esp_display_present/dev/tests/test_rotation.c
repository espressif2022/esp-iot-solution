/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_presenter.h"
#include "test_panel.h"
#include "scan_panel.h"
#include "../../src/present_os.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define OK(x) assert((x) == ESP_OK)
/* Link-time allocation accounting is local to this test; production allocators
 * are unchanged. This measures pixel pools, not libc/pthread allocations. */
void *__real_present_alloc_pixels(size_t size);
void __real_present_free(void *memory);
static struct {
    void *pointer;
    size_t bytes;
} allocations[128];
static size_t pixel_bytes;
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
void *__wrap_present_alloc_pixels(size_t size)
{
    void *pointer = __real_present_alloc_pixels(size);
    if (!pointer) {
        return NULL;
    }
    pthread_mutex_lock(&allocation_lock);
    size_t slot = 0;
    while (slot < 128 && allocations[slot].pointer) {
        slot++;
    }
    assert(slot < 128);
    allocations[slot].pointer = pointer;
    allocations[slot].bytes = size;
    pixel_bytes += size;
    pthread_mutex_unlock(&allocation_lock);
    return pointer;
}
void *__wrap_present_alloc_pixels_in(size_t size, bool external)
{
    (void)external; /* Host has one address space; account both allocation paths. */
    return __wrap_present_alloc_pixels(size);
}
void __wrap_present_free(void *memory)
{
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < 128; i++) {
        if (memory && allocations[i].pointer == memory) {
            pixel_bytes -= allocations[i].bytes;
            allocations[i].pointer = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&allocation_lock);
    __real_present_free(memory);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned count;
} notifications_t;
static bool notify(void *user, bool isr)
{
    assert(!isr);
    notifications_t *n = user;
    pthread_mutex_lock(&n->lock);
    n->count++;
    pthread_cond_broadcast(&n->changed);
    pthread_mutex_unlock(&n->lock);
    return false;
}

static unsigned count(notifications_t *n)
{
    pthread_mutex_lock(&n->lock);
    unsigned value = n->count;
    pthread_mutex_unlock(&n->lock);
    return value;
}

static void wait_changed(notifications_t *n, unsigned seen)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 2;
    pthread_mutex_lock(&n->lock);
    while (n->count == seen) {
        assert(pthread_cond_timedwait(&n->changed, &n->lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&n->lock);
}

static void pump(unsigned kind, present_backend_t *b)
{
    OK(kind == 0 ? present_gram_service(b) : kind == 1 ? present_te_service(b) : present_fb_service(b));
}

static void exercise(unsigned kind, esp_display_present_render_contract_t contract, bool async, size_t bpp, bool external, unsigned initial)
{
    const uint8_t expected[4][6] = {{1, 2, 3, 4, 5, 6}, {5, 3, 1, 6, 4, 2}, {6, 5, 4, 3, 2, 1}, {2, 4, 6, 1, 3, 5}};
    const present_backend_ops_t *ops = kind == 0 ? &present_gram_backend : kind == 1 ? &present_te_backend : &present_fb_backend;
    test_panel_t io = {.width = 3, .height = 2, .bpp = bpp};
    scan_panel_t scan = {0};
    notifications_t n = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    void *buffers[2] = {NULL, NULL};
    if (external) {
        buffers[0] = aligned_alloc(64, 64);
        buffers[1] = aligned_alloc(64, 64);
        assert(buffers[0] && buffers[1]);
    }
    esp_display_present_backend_config_t cfg = {.width = initial % 2 ? 2 : 3, .height = initial % 2 ? 3 : 2, .rotation = initial, .tile_rows = initial % 2 ? 3 : 2, .buffer_count = 2, .max_frames = 2,
                                                .format = bpp == 2 ? ESP_DISPLAY_PRESENT_FORMAT_RGB565 : ESP_DISPLAY_PRESENT_FORMAT_RGB888, .render_contract = contract,
                                                .panel = kind == 2 ? scan_interface(&scan) : test_panel_interface(&io),
                                                .te_window_us = 1000000, .te_transfer_budget_us = 1, .async_copy = async,
                                                .on_progress = notify, .progress_user = &n, .external_buffers = external ? buffers : NULL,
                                                .external_buffer_bytes = external ? 64 : 0
                                               };
    present_backend_t *b;
    size_t before_create = pixel_bytes;
    OK(ops->create(&cfg, &b));
    size_t zero_rotation_bytes = pixel_bytes - (kind == 2 && initial && contract != ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION ? 6 * bpp : 0);
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        unsigned rotation = (initial + iteration) % 4;
        OK(ops->set_rotation(b, rotation));
        if (kind == 2) {
            assert(pixel_bytes == zero_rotation_bytes + (rotation && contract != ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION ? 6 * bpp : 0));
        }
        esp_display_present_backend_caps_t caps;
        OK(ops->get_caps(b, &caps));
        assert(caps.draw_layout.width == (rotation % 2 ? 2u : 3u));
        assert(caps.draw_layout.height == (rotation % 2 ? 3u : 2u));
        for (uint32_t y = 0; y < caps.draw_layout.height; ++y)
            for (uint32_t x = 0; x < caps.draw_layout.width; ++x) {
                esp_display_present_rect_t a = {x, y, 1, 1}, physical;
                OK(esp_display_present_rotate_rect(caps.draw_layout.width, caps.draw_layout.height, rotation, &a, &physical));
                uint32_t lx, ly;
                OK(esp_display_present_unrotate_point(caps.draw_layout.width, caps.draw_layout.height, rotation, physical.x, physical.y, &lx, &ly));
                assert(lx == x && ly == y);
            }
        esp_display_present_frame_request_t req = {.full_redraw = true};
        esp_display_present_frame_t f;
        OK(ops->begin(b, &req, &f));
        assert(ops->set_rotation(b, rotation) == ESP_ERR_INVALID_STATE);
        esp_display_present_lease_t lease;
        for (;;) {
            unsigned seen = count(&n);
            esp_err_t error = ops->acquire(b, f, NULL, 0, &lease);
            if (error == ESP_OK) {
                break;
            }
            assert(error == ESP_ERR_TIMEOUT && async);
            wait_changed(&n, seen);
        }
        if (kind == 2 && external && contract != ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION) {
            if (rotation == 0) {
                assert(lease.surface.pixels == buffers[0] || lease.surface.pixels == buffers[1]);
            } else {
                assert(lease.surface.pixels != buffers[0] && lease.surface.pixels != buffers[1]);
            }
            assert(lease.surface.pixels != scan.current.pixels);
        }
        for (size_t i = 0; i < 6; ++i) {
            uint8_t *pixel = (uint8_t *)lease.surface.pixels + i * bpp;
            pixel[0] = i + 1;
            pixel[1] = 0x66;
            if (bpp == 3) {
                pixel[2] = 0x99;
            }
        }
        OK(ops->submit_region(b, f, lease.id));
        esp_display_present_receipt_t receipt;
        OK(ops->end(b, f, &receipt));
        for (;;) {
            unsigned seen = count(&n);
            pump(kind, b);
            if (kind == 1) {
                esp_display_present_backend_event_t edge = {.kind = ESP_DISPLAY_PRESENT_EVENT_TE_EDGE, .timestamp_us = present_now_us()};
                ops->on_event(b, &edge);
                pump(kind, b);
            }
            if (kind == 2 ? scan.cookie != 0 : io.count != 0) {
                break;
            }
            assert(async);
            wait_changed(&n, seen);
        }
        if (kind == 2) {
            scan_confirm(&scan);
        } else {
            test_panel_complete(&io);
        }
        esp_display_present_frame_status_t status;
        OK(ops->query(b, receipt, &status));
        assert(status.terminal && status.result == ESP_OK);
        const uint8_t *pixels = kind == 2 ? scan.pixels : io.pixels;
        for (size_t i = 0; i < 6; ++i) {
            assert(pixels[i * bpp] == expected[rotation][i]);
            assert(pixels[i * bpp + 1] == 0x66);
            if (bpp == 3) {
                assert(pixels[i * bpp + 2] == 0x99);
            }
        }
        OK(ops->release_receipt(b, receipt));
    }
    OK(ops->stop(b, 1000));
    OK(ops->destroy(b));
    assert(pixel_bytes == before_create);
    free(buffers[0]);
    free(buffers[1]);
    pthread_cond_destroy(&n.changed);
    pthread_mutex_destroy(&n.lock);
}
static void exercise_swapped_fb(esp_display_present_render_contract_t contract, bool async)
{
    const uint8_t expected[4][6] = {{1, 2, 3, 4, 5, 6}, {5, 3, 1, 6, 4, 2}, {6, 5, 4, 3, 2, 1}, {2, 4, 6, 1, 3, 5}};
    const present_backend_ops_t *ops = &present_fb_backend;
    scan_panel_t scan = {0};
    notifications_t n = {.lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    void *buffers[2] = {aligned_alloc(64, 64), aligned_alloc(64, 64)};
    assert(buffers[0] && buffers[1]);
    esp_display_present_backend_config_t cfg = {.width = 3, .height = 2, .tile_rows = 2, .buffer_count = 2, .max_frames = 2,
                                                .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565, .draw_order = ESP_DISPLAY_PRESENT_ORDER_NATIVE, .panel_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED,
                                                .render_contract = contract, .panel = scan_interface(&scan), .async_copy = async,
                                                .on_progress = notify, .progress_user = &n, .external_buffers = buffers, .external_buffer_bytes = 64
                                               };
    present_backend_t *b;
    size_t before_create = pixel_bytes;
    OK(ops->create(&cfg, &b));
    /* Even at 0 deg, draw_order != panel_order allocates one logical canvas. */
    assert(pixel_bytes == before_create + 12);
    for (unsigned rotation = 0; rotation < 4; ++rotation) {
        OK(ops->set_rotation(b, rotation));
        assert(pixel_bytes == before_create + 12);
        esp_display_present_frame_request_t req = {.full_redraw = true};
        esp_display_present_frame_t f;
        OK(ops->begin(b, &req, &f));
        esp_display_present_lease_t lease;
        for (;;) {
            unsigned seen = count(&n);
            esp_err_t error = ops->acquire(b, f, NULL, 0, &lease);
            if (error == ESP_OK) {
                break;
            }
            assert(error == ESP_ERR_TIMEOUT && async);
            wait_changed(&n, seen);
        }
        assert(lease.surface.layout.byte_order == ESP_DISPLAY_PRESENT_ORDER_NATIVE);
        assert(lease.surface.pixels != buffers[0] && lease.surface.pixels != buffers[1]);
        for (size_t i = 0; i < 6; ++i) {
            uint8_t *pixel = (uint8_t *)lease.surface.pixels + i * 2;
            pixel[0] = i + 1;
            pixel[1] = 0x66;
        }
        OK(ops->submit_region(b, f, lease.id));
        esp_display_present_receipt_t receipt;
        OK(ops->end(b, f, &receipt));
        for (;;) {
            unsigned seen = count(&n);
            pump(2, b);
            if (scan.cookie != 0) {
                break;
            }
            assert(async);
            wait_changed(&n, seen);
        }
        scan_confirm(&scan);
        assert(scan.current.layout.byte_order == ESP_DISPLAY_PRESENT_ORDER_SWAPPED);
        esp_display_present_frame_status_t status;
        OK(ops->query(b, receipt, &status));
        assert(status.terminal && status.result == ESP_OK);
        for (size_t i = 0; i < 6; ++i) {
            assert(scan.pixels[i * 2] == 0x66);
            assert(scan.pixels[i * 2 + 1] == expected[rotation][i]);
        }
        OK(ops->release_receipt(b, receipt));
    }
    OK(ops->stop(b, 1000));
    OK(ops->destroy(b));
    assert(pixel_bytes == before_create);
    free(buffers[0]);
    free(buffers[1]);
    pthread_cond_destroy(&n.changed);
    pthread_mutex_destroy(&n.lock);
}
int main(void)
{
    for (unsigned initial = 0; initial < 4; ++initial)
        for (unsigned kind = 0; kind < 3; ++kind)
            for (unsigned async = 0; async < 2; ++async)
                for (size_t bpp = 2; bpp <= 3; ++bpp) {
                    exercise(kind, ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION, async, bpp, false, initial);
                    if (kind == 2) {
                        exercise(kind, ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT, async, bpp, true, initial);
                        exercise(kind, ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL, async, bpp, true, initial);
                    }
                }
    for (unsigned async = 0; async < 2; ++async) {
        exercise_swapped_fb(ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_FULL, async);
        exercise_swapped_fb(ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_DIRECT, async);
    }
    puts("rotation: all backends, runtime orientation, inverse touch, async and external scanout passed");
    return 0;
}
