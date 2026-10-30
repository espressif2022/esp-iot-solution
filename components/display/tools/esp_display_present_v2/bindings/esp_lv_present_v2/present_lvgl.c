/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_lvgl.h"
#include <limits.h>
#include <string.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR < 5
#error "Experimental binding requires LVGL 9.5 or newer within major 9"
#endif

struct pv2_lvgl {
    pv2_presenter_t *presenter;
    lv_display_t *display;
    lv_draw_buf_t *draw;
    pv2_backend_caps_t caps;
    size_t bpp;
    bool refreshing;
    bool zero_copy;
    lv_draw_buf_t borrowed;
    pv2_lease_t lease;
    esp_err_t error;
};

static esp_err_t copy_flush(pv2_lvgl_t *b, const lv_area_t *area, uint8_t *pixels)
{
    if (!area || !pixels || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 || area->y2 < area->y1 ||
            (uint32_t)area->x2 >= b->caps.draw_layout.width || (uint32_t)area->y2 >= b->caps.draw_layout.height) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t width = (uint32_t)(area->x2 - area->x1) + 1;
    uint32_t height = (uint32_t)(area->y2 - area->y1) + 1;
    if (width > SIZE_MAX / b->bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t row = width * b->bpp;
    lv_draw_buf_t *active = lv_display_get_buf_active(b->display);
    if (!active || !active->data) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t stride = active->header.stride;
    bool full_surface = b->caps.contract != PV2_CONTRACT_PARTITION;
    uintptr_t base = (uintptr_t)active->data, ptr = (uintptr_t)pixels;
    if (full_surface) {
        /* LVGL FULL/DIRECT passes the full draw-buffer base, even for dirty areas. */
        if (!stride || ptr != base || (size_t)area->y1 > SIZE_MAX / stride) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t offset = (size_t)area->y1 * stride;
        size_t column = (size_t)area->x1 * b->bpp;
        if (offset > active->data_size || column > active->data_size - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        pixels += offset + column;
        ptr = (uintptr_t)pixels;
    }
    if (stride < row || height - 1 > (SIZE_MAX - row) / stride ||
            ptr < base || ptr - base > active->data_size ||
            (height - 1) * stride + row > active->data_size - (ptr - base)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!b->refreshing) {
        const pv2_frame_request_t request = {0};
        esp_err_t ret = pv2_presenter_begin_next_frame(b->presenter, &request);
        if (ret != ESP_OK) {
            return ret;
        }
        b->refreshing = true;
    }
    for (uint32_t offset = 0; offset < height;) {
        pv2_lease_t lease;
        esp_err_t ret = pv2_presenter_acquire_buffer(b->presenter, &lease);
        if (ret != ESP_OK) {
            return ret;
        }
        uint32_t rows = height - offset;
        if (!full_surface) {
            ret = pv2_presenter_resolve_rows(b->presenter, &lease, row, height - offset, &rows);
            if (ret != ESP_OK) {
                return ret;
            }
        }
        size_t destination_stride = full_surface ? lease.surface.layout.stride_bytes : row;
        if (!rows) {
            return ESP_ERR_INVALID_SIZE;
        }
        for (uint32_t y = 0; y < rows; ++y) {
            size_t destination_offset = full_surface ?
                                        ((size_t)area->y1 + offset + y) * destination_stride + (size_t)area->x1 * b->bpp : y * row;
            if (destination_offset > lease.surface.capacity_bytes || row > lease.surface.capacity_bytes - destination_offset) {
                return ESP_ERR_INVALID_SIZE;
            }
            uint8_t *dst = (uint8_t *)lease.surface.pixels + destination_offset;
            const uint8_t *src = pixels + (offset + y) * stride;
            if (b->bpp == 2) {
                memcpy(dst, src, row);
            } else {
                /* LVGL RGB888 stores lv_color_t as B,G,R; PV2_FORMAT_RGB888 is R,G,B.
                 * Normalize channels while copying, independently of stride. */
                _Static_assert(sizeof(lv_color_t) == 3, "Unexpected LVGL RGB888 representation");
                for (size_t x = 0; x < width; ++x) {
                    lv_color_t color;
                    memcpy(&color, src + x * 3, sizeof(color));
                    dst[x * 3] = color.red;
                    dst[x * 3 + 1] = color.green;
                    dst[x * 3 + 2] = color.blue;
                }
            }
        }
        pv2_area_t band = { area->x1, area->y1 + (int32_t)offset,
                            area->x2, area->y1 + (int32_t)(offset + rows - 1)
                          };
        ret = pv2_presenter_submit_buffer(b->presenter, &lease, &band, destination_stride);
        if (ret != ESP_OK) {
            return ret;
        }
        offset += rows;
    }
    return ESP_OK;
}

static void detach(pv2_lvgl_t *b)
{
    lv_display_set_draw_buffers(b->display, b->draw, NULL);
}

static void fail_refresh(pv2_lvgl_t *b, esp_err_t error)
{
    if (b->error == ESP_OK) {
        b->error = error;
    }
    lv_timer_pause(lv_display_get_refr_timer(b->display));
    if (b->zero_copy) {
        detach(b);
    }
    if (pv2_presenter_cancel_frame(b->presenter) == ESP_OK) {
        b->refreshing = false;
    }
}

static void refresh_event(lv_event_t *event)
{
    pv2_lvgl_t *b = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_REFR_READY) {
        /* Defensive cleanup if rendering ended without a submitted full flush. */
        detach(b);
        if (b->refreshing) {
            esp_err_t ret = pv2_presenter_cancel_frame(b->presenter);
            if (ret == ESP_OK) {
                b->refreshing = false;
            } else {
                fail_refresh(b, ret);
            }
        }
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_RENDER_START || b->error != ESP_OK) {
        return;
    }
    const pv2_frame_request_t request = {0};
    esp_err_t ret = pv2_presenter_begin_next_frame(b->presenter, &request);
    if (ret != ESP_OK) {
        fail_refresh(b, ret);
        return;
    }
    b->refreshing = true;
    ret = pv2_presenter_acquire_buffer(b->presenter, &b->lease);
    if (ret != ESP_OK) {
        fail_refresh(b, ret);
        return;
    }
    if (b->lease.surface.capacity_bytes > UINT32_MAX ||
            lv_draw_buf_init(&b->borrowed, b->caps.draw_layout.width, b->caps.draw_layout.height,
                             LV_COLOR_FORMAT_RGB565, b->caps.draw_layout.stride_bytes, b->lease.surface.pixels,
                             b->lease.surface.capacity_bytes) != LV_RESULT_OK) {
        fail_refresh(b, ESP_ERR_INVALID_SIZE);
        return;
    }
    lv_display_set_draw_buffers(b->display, &b->borrowed, NULL);
}

static esp_err_t submit_borrowed(pv2_lvgl_t *b, const lv_area_t *area, uint8_t *pixels)
{
    if (!b->refreshing || pixels != b->lease.surface.pixels || !area || area->x1 || area->y1 ||
            area->x2 != (int32_t)b->caps.draw_layout.width - 1 || area->y2 != (int32_t)b->caps.draw_layout.height - 1 ||
            !lv_display_flush_is_last(b->display)) {
        return ESP_ERR_INVALID_STATE;
    }
    const pv2_area_t full = {area->x1, area->y1, area->x2, area->y2};
    return pv2_presenter_submit_buffer(b->presenter, &b->lease, &full, b->lease.surface.layout.stride_bytes);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    pv2_lvgl_t *b = lv_display_get_user_data(display);
    if (b->error == ESP_OK) {
        esp_err_t ret = b->zero_copy ? submit_borrowed(b, area, pixels) : copy_flush(b, area, pixels);
        if (ret == ESP_OK && lv_display_flush_is_last(display)) {
            ret = pv2_presenter_commit_frame(b->presenter, NULL);
            if (ret == ESP_OK) {
                b->refreshing = false;
            }
        }
        if (ret != ESP_OK) {
            fail_refresh(b, ret);
        }
    }
    if (b->zero_copy) {
        detach(b);
    }
    /* All source reads are synchronous and finished. DMA references private
     * leases only, so LVGL can reshape/reuse this buffer even on error. */
    lv_display_flush_ready(display);
}

static esp_err_t create_binding(pv2_presenter_t *presenter, uint32_t rows, bool zero_copy, pv2_lvgl_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!presenter || !rows) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lv_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_backend_caps_t caps;
    esp_err_t ret = pv2_presenter_get_caps(presenter, &caps);
    if (ret != ESP_OK) {
        return ret;
    }
    if (caps.contract > PV2_CONTRACT_DIRECT || caps.draw_layout.byte_order != PV2_ORDER_NATIVE ||
            (caps.draw_layout.format != PV2_FORMAT_RGB565 && caps.draw_layout.format != PV2_FORMAT_RGB888)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    pv2_presenter_status_t status;
    ret = pv2_presenter_get_status(presenter, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    if (status.state != PV2_PRESENTER_RUNNING || status.frame_active || status.pending_frames || status.first_error != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (caps.contract != PV2_CONTRACT_PARTITION) {
        rows = caps.draw_layout.height;
    }
    if (rows > caps.draw_layout.height) {
        rows = caps.draw_layout.height;
    }
    size_t bpp = caps.draw_layout.format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (caps.draw_layout.width > INT_MAX || caps.draw_layout.height > INT_MAX ||
            caps.draw_layout.width > UINT32_MAX / bpp) {
        return ESP_ERR_INVALID_SIZE;
    }
    lv_color_format_t cf = bpp == 2 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_RGB888;
    uint32_t stride = lv_draw_buf_width_to_stride(caps.draw_layout.width, cf);
    if (zero_copy && (caps.contract != PV2_CONTRACT_FULL || bpp != 2 ||
                      caps.draw_layout.stride_bytes != stride || caps.writes_active_scanout)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!stride || rows > UINT32_MAX / stride) {
        return ESP_ERR_INVALID_SIZE;
    }
    pv2_lvgl_t *b = lv_malloc_zeroed(sizeof(*b));
    if (!b) {
        return ESP_ERR_NO_MEM;
    }
    b->presenter = presenter;
    b->caps = caps;
    b->bpp = bpp;
    b->zero_copy = zero_copy;
    b->draw = lv_draw_buf_create(caps.draw_layout.width, rows, cf, stride);
    if (!b->draw) {
        lv_free(b);
        return ESP_ERR_NO_MEM;
    }
    b->display = lv_display_create((int32_t)caps.draw_layout.width, (int32_t)caps.draw_layout.height);
    if (!b->display) {
        lv_draw_buf_destroy(b->draw);
        lv_free(b);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(b->display, cf);
    lv_display_set_user_data(b->display, b);
    lv_display_set_draw_buffers(b->display, b->draw, NULL);
    lv_display_render_mode_t mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
    switch (caps.contract) {
    case PV2_CONTRACT_FULL:
        mode = LV_DISPLAY_RENDER_MODE_FULL;
        break;
    case PV2_CONTRACT_DIRECT:
        mode = LV_DISPLAY_RENDER_MODE_DIRECT;
        break;
    default:
        break;
    }
    lv_display_set_render_mode(b->display, mode);
    lv_display_set_flush_cb(b->display, flush);
    if (zero_copy) {
        lv_display_add_event_cb(b->display, refresh_event, LV_EVENT_RENDER_START, b);
        lv_display_add_event_cb(b->display, refresh_event, LV_EVENT_REFR_READY, b);
    }
    /* TE has no baseline until actual full coverage has been composed. */
    lv_obj_invalidate(lv_display_get_screen_active(b->display));
    *out = b;
    return ESP_OK;
}

esp_err_t pv2_lvgl_create(pv2_presenter_t *p, uint32_t rows, pv2_lvgl_t **out)
{
    return create_binding(p, rows, false, out);
}

esp_err_t pv2_lvgl_create_zero_copy(pv2_presenter_t *p, pv2_lvgl_t **out)
{
    return create_binding(p, 1, true, out);
}

lv_display_t *pv2_lvgl_get_display(pv2_lvgl_t *b)
{
    return b ? b->display : NULL;
}

esp_err_t pv2_lvgl_get_error(pv2_lvgl_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_status_t status;
    esp_err_t ret = pv2_presenter_get_status(b->presenter, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    if (b->error == ESP_OK && status.first_error != ESP_OK) {
        b->error = status.first_error;
        lv_timer_pause(lv_display_get_refr_timer(b->display));
    }
    return b->error;
}

esp_err_t pv2_lvgl_destroy(pv2_lvgl_t *b)
{
    if (!b) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_presenter_status_t status;
    esp_err_t ret = pv2_presenter_get_status(b->presenter, &status);
    if (ret != ESP_OK) {
        return ret;
    }
    lv_timer_pause(lv_display_get_refr_timer(b->display));
    ret = pv2_presenter_cancel_frame(b->presenter);
    if (ret != ESP_OK) {
        return ret;
    }
    lv_display_delete(b->display);
    lv_draw_buf_destroy(b->draw);
    lv_free(b);
    return ESP_OK;
}
