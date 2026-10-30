/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_pixel_accel.h"
#include "driver/ppa.h"
#include "esp_private/async_memcpy_dma2d.h"
#include "esp_cache.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#define OK(x) assert((x)==ESP_OK)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool defer, entered;
static async_memcpy_dma2d_trans_desc_t pending;
static async_memcpy_dma2d_isr_cb_t pending_cb;
static void *pending_user;
static unsigned busy_count;
static unsigned submissions, flushes, invalidates;
static void *expected_sync;
static size_t expected_sync_bytes;
static esp_err_t result;
static size_t alignment = 64, strict_alignment = 1;
size_t dma2d_get_buffer_alignment_constraint(const void *p)
{
    (void)p;
    return strict_alignment;
}
size_t esp_cache_get_line_size_by_addr(const void *p)
{
    (void)p;
    return alignment;
}

esp_err_t esp_cache_msync(void *p, size_t n, int flags)
{
    assert(!((uintptr_t)p % alignment) && !(n % alignment));
    if (expected_sync) {
        assert(p == expected_sync && n == expected_sync_bytes);
    }
    if (flags == ESP_CACHE_MSYNC_FLAG_DIR_C2M) {
        flushes++;
    } else {
        assert(flags == ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        invalidates++;
    }
    return ESP_OK;
}

esp_err_t ppa_register_client(const ppa_client_config_t *c, ppa_client_handle_t *h)
{
    assert(c->max_pending_trans_num == 1);
    *h = (void *)1;
    return ESP_OK;
}

esp_err_t ppa_unregister_client(ppa_client_handle_t h)
{
    assert(h);
    return result;
}

esp_err_t esp_async_memcpy_install_dma2d(const async_memcpy_dma2d_config_t *c, async_memcpy_dma2d_handle_t *h)
{
    assert(c->backlog == 1);
    *h = (void *)2;
    return ESP_OK;
}

esp_err_t esp_async_memcpy_uninstall_dma2d(async_memcpy_dma2d_handle_t h)
{
    assert(h);
    return result;
}
/* Independent descriptor interpreter: no production pixel helper in the mock. */
static void paint(const uint8_t *src, size_t ss, uint8_t *dst, size_t ds, unsigned x, unsigned y, unsigned w, unsigned h, unsigned dx, unsigned dy, unsigned rot, unsigned bpp, bool swap)
{
    for (unsigned j = 0; j < h; j++)
        for (unsigned i = 0; i < w; i++) {
            unsigned ox = i, oy = j;
            if (rot == 1) {
                ox = h - 1 - j;
                oy = i;
            }
            if (rot == 2) {
                ox = w - 1 - i;
                oy = h - 1 - j;
            }
            if (rot == 3) {
                ox = j;
                oy = w - 1 - i;
            }
            for (unsigned k = 0; k < bpp; k++) {
                dst[(dy + oy)*ds + (dx + ox)*bpp + k] = src[(y + j) * ss + (x + i) * bpp + (swap ? bpp - 1 - k : k)];
            }
        }
}

esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t h, const ppa_srm_oper_config_t *o)
{
    assert(h && o->scale_x == 1 && o->scale_y == 1);
    submissions++;
    if (result) {
        return result;
    }
    unsigned bpp = o->in.srm_cm == PPA_SRM_COLOR_MODE_RGB565 ? 2 : 3;
    paint(o->in.buffer, o->in.pic_w * bpp, o->out.buffer, o->out.pic_w * bpp, o->in.block_offset_x, o->in.block_offset_y, o->in.block_w, o->in.block_h, o->out.block_offset_x, o->out.block_offset_y, (4 - o->rotation_angle) % 4, bpp, o->byte_swap);
    return ESP_OK;
}

esp_err_t esp_async_memcpy_dma2d(async_memcpy_dma2d_handle_t h, const async_memcpy_dma2d_trans_desc_t *o, async_memcpy_dma2d_isr_cb_t cb, void *user)
{
    submissions++;
    if (busy_count) {
        busy_count--;
        return ESP_ERR_INVALID_STATE;
    }
    if (result) {
        return result;
    }
    pthread_mutex_lock(&lock);
    if (defer) {
        pending = *o;
        pending_cb = cb;
        pending_user = user;
        entered = true;
        pthread_cond_broadcast(&changed);
        pthread_mutex_unlock(&lock);
        return ESP_OK;
    }
    pthread_mutex_unlock(&lock);
    unsigned bpp = o->pixel_format == ESP_COLOR_FOURCC_RGB16 ? 2 : 3;
    paint(o->src_buffer, o->src_stride * bpp, o->dst_buffer, o->dst_stride * bpp, o->src_x, o->src_y, o->copy_width, o->copy_height, o->dst_x, o->dst_y, 0, bpp, false);
    cb(h, NULL, user); /* Completion before submit returns must not be lost. */
    return ESP_OK;
}

static void cache_bands(void)
{
    uint8_t *src = aligned_alloc(128, 3072), *dst = aligned_alloc(128, 3072), expected[3072];
    assert(src && dst);
    for (size_t i = 0; i < 3072; i++) {
        src[i] = (uint8_t)i;
    }
    alignment = 64;
    for (unsigned rotation = 0; rotation < 4; rotation++) {
        pv2_pixel_accel_t *a;
        pv2_pixel_accel_config_t cfg = {PV2_ACCEL_PPA, false};
        OK(pv2_pixel_accel_create(&cfg, &a));
        pv2_layout_t layout = {.width = 32, .height = 32, .stride_bytes = 96, .format = PV2_FORMAT_RGB565};
        pv2_copy_request_t r = {.source = {src, 3072, layout}, .destination = {dst, 3072, layout},
                                .source_area = {1, 1, 3, 2}, .destination_x = 1, .destination_y = 3, .rotation = rotation
                               };
        memset(dst, 0xa5, 3072);
        memset(expected, 0xa5, 3072);
        expected_sync = dst + 256;
        expected_sync_bytes = rotation % 2 ? 320 : 256;
        unsigned before = invalidates;
        OK(pv2_pixel_accel_execute(a, &r));
        assert(invalidates == before + 1);
        paint(src, 96, expected, 96, 1, 1, 3, 2, 1, 3, rotation, 2, false);
        assert(!memcmp(dst, expected, 3072));
        /* Last physical row and allocation boundary, quarter turns included. */
        r.destination_y = rotation % 2 ? 29 : 30;
        expected_sync = dst + (rotation % 2 ? 2752 : 2880);
        expected_sync_bytes = rotation % 2 ? 320 : 192;
        OK(pv2_pixel_accel_execute(a, &r));
        OK(pv2_pixel_accel_destroy(a));
    }
    expected_sync = NULL;
    free(src);
    free(dst);
}
int main(void)
{
    cache_bands();
    uint8_t *src = aligned_alloc(128, 128), *dst = aligned_alloc(128, 128), expected[128];
    assert(src && dst);
    for (unsigned i = 0; i < 128; i++) {
        src[i] = (uint8_t)i;
    }
    for (unsigned kind = 0; kind < 2; kind++)
        for (unsigned bpp = 2; bpp <= 3; bpp++)
            for (unsigned rot = 0; rot < 4; rot++) {
                pv2_pixel_accel_t *a;
                pv2_pixel_accel_config_t c = {kind, true};
                OK(pv2_pixel_accel_create(&c, &a));
                memset(dst, 0xaa, 128);
                memset(expected, 0xaa, 128);
                pv2_layout_t layout = {.width = 5, .height = 5, .stride_bytes = 5 * bpp, .format = bpp == 2 ? PV2_FORMAT_RGB565 : PV2_FORMAT_RGB888};
                pv2_copy_request_t r = {.source = {src, 128, layout}, .destination = {dst, 128, layout}, .source_area = {1, 1, 3, 2}, .destination_x = 1, .destination_y = 1, .rotation = rot};
                if (bpp == 2 && kind == 1) {
                    r.destination.layout.byte_order = PV2_ORDER_SWAPPED;
                }
                paint(src, 5 * bpp, expected, 5 * bpp, 1, 1, 3, 2, 1, 1, rot, bpp, bpp == 2 && kind == 1);
                unsigned count = submissions;
                OK(pv2_pixel_accel_execute(a, &r));
                assert(!memcmp(dst, expected, 128));
                assert(submissions == count + (kind == 1 || rot == 0));
                result = ESP_FAIL;
                assert(pv2_pixel_accel_destroy(a) == ESP_FAIL);
                result = ESP_OK;
                OK(pv2_pixel_accel_destroy(a));
            }
    pv2_pixel_accel_t *a;
    /* Source without tail cache-line padding (32B capacity on 64B line) must still be admitted by hardware. */
    {
        alignment = 64;
        strict_alignment = 1;
        pv2_layout_t l4 = {.width = 4, .height = 4, .stride_bytes = 8, .format = PV2_FORMAT_RGB565};
        pv2_copy_request_t r32 = {.source = {src, 32, l4}, .destination = {dst, 64, l4}, .source_area = {0, 0, 4, 4}};
        for (unsigned kind = 0; kind < 2; kind++) {
            pv2_pixel_accel_config_t cfg32 = {kind, false};
            OK(pv2_pixel_accel_create(&cfg32, &a));
            unsigned before_sub = submissions;
            OK(pv2_pixel_accel_execute(a, &r32));
            assert(submissions == before_sub + 1);
            OK(pv2_pixel_accel_destroy(a));
        }
    }
    pv2_pixel_accel_config_t c = {PV2_ACCEL_DMA2D, false};
    OK(pv2_pixel_accel_create(&c, &a));
    pv2_layout_t l = {.width = 2, .height = 2, .stride_bytes = 4, .format = PV2_FORMAT_RGB565};
    pv2_copy_request_t r = {.source = {src, 128, l}, .destination = {dst, 64, l}, .source_area = {0, 0, 2, 2}};
    alignment = 128;
    assert(pv2_pixel_accel_execute(a, &r) == ESP_ERR_NOT_SUPPORTED);
    r.destination.capacity_bytes = 128;
    memset(dst, 0xaa, 128);
    result = ESP_FAIL;
    assert(pv2_pixel_accel_execute(a, &r) == ESP_FAIL);
    assert(dst[0] == 0xaa); /* No CPU fallback after hardware error. */
    result = ESP_OK;
    busy_count = 1;
    OK(pv2_pixel_accel_execute(a, &r));
    assert(!busy_count);
    strict_alignment = 16;
    assert(pv2_pixel_accel_execute(a, &r) == ESP_ERR_NOT_SUPPORTED);
    strict_alignment = 1;
    OK(pv2_pixel_accel_destroy(a));
    c.kind = PV2_ACCEL_PPA;
    OK(pv2_pixel_accel_create(&c, &a));
    strict_alignment = 16;
    assert(pv2_pixel_accel_execute(a, &r) == ESP_ERR_NOT_SUPPORTED);
    strict_alignment = 1;
    OK(pv2_pixel_accel_destroy(a));
    c.kind = PV2_ACCEL_DMA2D;
    OK(pv2_pixel_accel_create(&c, &a));
    pv2_pixel_config_t pc = {.max_jobs = 1, .asynchronous = true, .execute = pv2_pixel_accel_execute, .execute_user = a};
    pv2_pixel_engine_t *engine;
    OK(pv2_pixel_create(&pc, &engine));
    pthread_mutex_lock(&lock);
    defer = true;
    pthread_mutex_unlock(&lock);
    pv2_copy_ticket_t ticket;
    OK(pv2_pixel_submit(engine, &r, &ticket));
    pthread_mutex_lock(&lock);
    while (!entered) {
        pthread_cond_wait(&changed, &lock);
    }
    pthread_mutex_unlock(&lock);
    assert(pv2_pixel_stop(engine, 0) == ESP_ERR_TIMEOUT); /* Hardware still borrows buffers. */
    paint(pending.src_buffer, pending.src_stride * 2, pending.dst_buffer, pending.dst_stride * 2, pending.src_x, pending.src_y, pending.copy_width, pending.copy_height, pending.dst_x, pending.dst_y, 0, 2, false);
    pending_cb((void*)2, NULL, pending_user);
    OK(pv2_pixel_stop(engine, UINT32_MAX));
    OK(pv2_pixel_release(engine, ticket));
    OK(pv2_pixel_destroy(engine));
    OK(pv2_pixel_accel_destroy(a));
    assert(flushes && invalidates);
    free(src);
    free(dst);
    puts("accel: descriptor pixels, alignment, fallback, early completion and failed teardown passed");
}
