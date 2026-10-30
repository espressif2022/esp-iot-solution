/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#include "present_pixel_accel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if PV2_HOST_PPA
#include "driver/ppa.h"
#endif
#if PV2_HOST_DMA2D
#include "esp_private/async_memcpy_dma2d.h"
#endif
#if PV2_HOST_PPA || PV2_HOST_DMA2D
#include "esp_cache.h"
static unsigned submissions, syncs;
static size_t strict_alignment = 1;
static esp_err_t driver_error, unregister_error;
size_t dma2d_get_buffer_alignment_constraint(const void *p)
{
    (void)p;
    return strict_alignment;
}
size_t esp_cache_get_line_size_by_addr(const void *p)
{
    (void)p;
    return 64;
}
esp_err_t esp_cache_msync(void *p, size_t n, int flags)
{
    (void)flags;
    assert((uintptr_t)p % 64 == 0 && n % 64 == 0);
    syncs++;
    return ESP_OK;
}
#endif
/* Independent pixel oracle, also used to interpret fake driver descriptors. */
static void paint(const uint8_t *src, size_t ss, uint8_t *dst, size_t ds,
                  unsigned sx, unsigned sy, unsigned w, unsigned h,
                  unsigned dx, unsigned dy, unsigned rot, unsigned bpp, bool swap)
{
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned ox = x, oy = y;
            if (rot == 1) {
                ox = h - 1 - y;
                oy = x;
            }
            if (rot == 2) {
                ox = w - 1 - x;
                oy = h - 1 - y;
            }
            if (rot == 3) {
                ox = y;
                oy = w - 1 - x;
            }
            for (unsigned b = 0; b < bpp; b++) {
                dst[(dy + oy)*ds + (dx + ox)*bpp + b] =
                    src[(sy + y) * ss + (sx + x) * bpp + (swap ? bpp - b - 1 : b)];
            }
        }
    }
}
#if PV2_HOST_PPA
esp_err_t ppa_register_client(const ppa_client_config_t *c, ppa_client_handle_t *h)
{
    assert(c->max_pending_trans_num == 1);
    *h = (void *)1;
    return ESP_OK;
}
esp_err_t ppa_unregister_client(ppa_client_handle_t h)
{
    assert(h);
    return unregister_error;
}
esp_err_t ppa_do_scale_rotate_mirror(ppa_client_handle_t h, const ppa_srm_oper_config_t *o)
{
    assert(h);
    submissions++;
    if (driver_error) {
        return driver_error;
    }
    unsigned bpp = o->in.srm_cm == PPA_SRM_COLOR_MODE_RGB565 ? 2 : 3;
    paint(o->in.buffer, o->in.pic_w * bpp, o->out.buffer, o->out.pic_w * bpp,
          o->in.block_offset_x, o->in.block_offset_y, o->in.block_w, o->in.block_h,
          o->out.block_offset_x, o->out.block_offset_y, (4 - o->rotation_angle) % 4, bpp, o->byte_swap);
    return ESP_OK;
}
#endif
#if PV2_HOST_DMA2D
esp_err_t esp_async_memcpy_install_dma2d(const async_memcpy_dma2d_config_t *c, async_memcpy_dma2d_handle_t *h)
{
    assert(c->backlog == 1);
    *h = (void *)2;
    return ESP_OK;
}
esp_err_t esp_async_memcpy_uninstall_dma2d(async_memcpy_dma2d_handle_t h)
{
    assert(h);
    return unregister_error;
}
esp_err_t esp_async_memcpy_dma2d(async_memcpy_dma2d_handle_t h, const async_memcpy_dma2d_trans_desc_t *o,
                                 async_memcpy_dma2d_isr_cb_t cb, void *user)
{
    submissions++;
    if (driver_error) {
        return driver_error;
    }
    unsigned bpp = o->pixel_format == ESP_COLOR_FOURCC_RGB16 ? 2 : 3;
    paint(o->src_buffer, o->src_stride * bpp, o->dst_buffer, o->dst_stride * bpp,
          o->src_x, o->src_y, o->copy_width, o->copy_height, o->dst_x, o->dst_y, 0, bpp, false);
    cb(h, NULL, user);
    return ESP_OK;
}
#endif
static void verify(pv2_executor_t *e, pv2_executor_t *cpu, bool is_dma)
{
    uint8_t *src = aligned_alloc(64, 192), *dst = aligned_alloc(64, 192), expected[192];
    assert(src && dst);
    for (size_t i = 0; i < 192; i++) {
        src[i] = (uint8_t)i;
    }
    pv2_executor_caps_t caps;
    assert(pv2_executor_get_caps(e, &caps) == ESP_OK);
    assert(caps.format_mask == 3 && caps.rotation_mask == (is_dma ? 1 : 15));
    for (unsigned bpp = 2; bpp <= 3; bpp++) {
        for (unsigned rot = 0; rot < 4; rot++) {
            pv2_layout_t layout = {5, 5, 6 * bpp, bpp == 2 ? PV2_FORMAT_RGB565 : PV2_FORMAT_RGB888, PV2_ORDER_NATIVE};
            pv2_copy_request_t r = {.source = {src, 192, layout}, .destination = {dst, 192, layout},
                                    .source_area = {1, 1, 3, 2}, .destination_x = 1, .destination_y = 1, .rotation = rot
                                   };
            if (bpp == 2 && !is_dma) {
                r.destination.layout.byte_order = PV2_ORDER_SWAPPED;
            }
            memset(dst, 0xa5, 192);
            memset(expected, 0xa5, 192);
            pv2_executor_reason_t why;
#if PV2_HOST_PPA || PV2_HOST_DMA2D
            unsigned before = submissions, before_sync = syncs;
#endif
            esp_err_t error = pv2_executor_probe(e, &r, &why);
            assert(error == (is_dma && rot ? ESP_ERR_NOT_SUPPORTED : ESP_OK));
            assert(why == (is_dma && rot ? PV2_EXEC_TRANSFORM : PV2_EXEC_SUPPORTED));
            assert(!memcmp(dst, expected, 192));
#if PV2_HOST_PPA || PV2_HOST_DMA2D
            assert(submissions == before && syncs == before_sync);
#endif
            pv2_executor_dispatch_t dispatch = {e, NULL};
            pv2_executor_selection_t selected;
            assert(pv2_executor_select(&dispatch, &r, &selected) == error);
            if (error) {
                assert(!selected.executor);
            }
            dispatch.fallback = cpu;
            assert(pv2_executor_run(&dispatch, &r, &selected) == ESP_OK);
            assert(selected.executor == (is_dma && rot ? cpu : e));
            assert(selected.used_fallback == (is_dma && rot != 0));
            paint(src, layout.stride_bytes, expected, layout.stride_bytes, 1, 1, 3, 2, 1, 1, rot, bpp, bpp == 2 && !is_dma);
            assert(!memcmp(dst, expected, 192));
            r.source_area.width = 100;
            assert(pv2_executor_run(&dispatch, &r, &selected) == ESP_ERR_INVALID_ARG);
            assert(!selected.executor && selected.primary_reason == PV2_EXEC_INVALID_REQUEST);
        }
    }
#if PV2_HOST_PPA || PV2_HOST_DMA2D
    if (e != cpu) {
        pv2_layout_t l = {4, 4, 8, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE};
        pv2_copy_request_t r = {.source = {src, 32, l}, .destination = {dst, 64, l}, .source_area = {0, 0, 4, 4}};
        pv2_executor_dispatch_t d = {e, cpu};
        pv2_executor_selection_t selected;
        unsigned before = submissions, before_sync = syncs;
        assert(pv2_executor_select(&d, &r, &selected) == ESP_OK && selected.executor == e);
        assert(submissions == before && syncs == before_sync);
        memset(dst, 0xa5, 192);
        driver_error = ESP_FAIL;
        assert(pv2_executor_run(&d, &r, &selected) == ESP_FAIL);
        assert(selected.executor == e && !selected.used_fallback && dst[0] == 0xa5);
        driver_error = ESP_OK;
        strict_alignment = 16;
        assert(pv2_executor_select(&d, &r, &selected) == ESP_OK && selected.used_fallback);
        assert(selected.primary_reason == (is_dma ? PV2_EXEC_ALIGNMENT : PV2_EXEC_MEMORY));
        strict_alignment = 1;
        unregister_error = ESP_FAIL;
        assert(pv2_executor_destroy(e) == ESP_FAIL);
        assert(pv2_executor_run(&d, &r, &selected) == ESP_OK && selected.executor == e);
        unregister_error = ESP_OK;
    }
#endif
    free(src);
    free(dst);
}
/* Arbitrary injected executor: chooser must not depend on built-in kind enums. */
typedef struct {
    unsigned calls;
    esp_err_t probe_error, execute_error;
} injected_t;
static esp_err_t injected_caps(void *context, pv2_executor_caps_t *c)
{
    (void)context;
    *c = (pv2_executor_caps_t) {
        .name = "injected"
    };
    return ESP_OK;
}
static esp_err_t injected_probe(void *context, const pv2_copy_request_t *r, pv2_executor_reason_t *why)
{
    (void)r;
    injected_t *i = context;
    *why = i->probe_error ? PV2_EXEC_LAYOUT : PV2_EXEC_SUPPORTED;
    return i->probe_error;
}
static esp_err_t injected_execute(void *context, const pv2_copy_request_t *r)
{
    (void)r;
    injected_t *i = context;
    i->calls++;
    return i->execute_error;
}
static esp_err_t injected_destroy(void *context)
{
    (void)context;
    return ESP_OK;
}
static void injection(pv2_executor_t *cpu)
{
    const pv2_executor_ops_t ops = {injected_caps, injected_probe, injected_execute, injected_destroy};
    injected_t state = {0};
    pv2_executor_t e = {&ops, &state};
    uint16_t src = 0x1234, dst = 0;
    pv2_layout_t l = {1, 1, 2, PV2_FORMAT_RGB565, PV2_ORDER_NATIVE};
    pv2_copy_request_t r = {.source = {&src, 2, l}, .destination = {&dst, 2, l}, .source_area = {0, 0, 1, 1}};
    pv2_executor_dispatch_t d = {&e, cpu};
    pv2_executor_selection_t selected;
    state.probe_error = ESP_ERR_INVALID_STATE;
    assert(pv2_executor_run(&d, &r, &selected) == ESP_ERR_INVALID_STATE && !state.calls && dst == 0);
    state.probe_error = ESP_OK;
    state.execute_error = ESP_ERR_NOT_SUPPORTED;
    assert(pv2_executor_run(&d, &r, &selected) == ESP_ERR_NOT_SUPPORTED && state.calls == 1 && dst == 0);
    state.probe_error = ESP_ERR_NOT_SUPPORTED;
    assert(pv2_executor_run(&d, &r, &selected) == ESP_OK && selected.used_fallback && dst == src);
    assert(pv2_executor_destroy(&e) == ESP_OK);
}
int main(void)
{
    pv2_executor_t *cpu, *e;
    assert(pv2_executor_cpu_create(&cpu) == ESP_OK);
    verify(cpu, cpu, false);
    injection(cpu);
    assert(pv2_executor_ppa_create(NULL) == ESP_ERR_INVALID_ARG);
    e = NULL;
#if PV2_HOST_PPA
    assert(pv2_executor_ppa_create(&e) == ESP_OK);
    verify(e, cpu, false);
    assert(pv2_executor_destroy(e) == ESP_OK);
#else
    assert(pv2_executor_ppa_create(&e) == ESP_ERR_NOT_SUPPORTED && !e);
    pv2_pixel_accel_t *a = NULL;
    pv2_pixel_accel_config_t c = {PV2_ACCEL_PPA, true};
    assert(pv2_pixel_accel_create(&c, &a) == ESP_ERR_NOT_SUPPORTED && !a);
#endif
    e = NULL;
#if PV2_HOST_DMA2D
    assert(pv2_executor_dma2d_create(&e) == ESP_OK);
    verify(e, cpu, true);
    assert(pv2_executor_destroy(e) == ESP_OK);
#else
    assert(pv2_executor_dma2d_create(&e) == ESP_ERR_NOT_SUPPORTED && !e);
#endif
    assert(pv2_executor_destroy(cpu) == ESP_OK);
    puts("executor matrix: capability, pure probe, injection, fallback, pixels and teardown passed");
    return 0;
}
