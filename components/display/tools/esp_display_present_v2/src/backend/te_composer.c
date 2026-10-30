/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "te_composer.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_coverage.h"
#include <string.h>

struct pv2_te_composer {
    pv2_pixel_engine_t *pixel;
    pv2_surface_t buffers[2];
    uint8_t *coverage;
    size_t coverage_bytes;
    int baseline, candidate;
    enum { IDLE, BUILDING, REPAIR, READY, INFLIGHT } state;
    pv2_te_frame_t frame;
    uint64_t ready_us, last_edge_us;
    bool edge_seen;
    struct {
        pv2_copy_ticket_t ticket;
        pv2_rect_t area;
    } writes[PV2_MAX_COPY_JOBS];
    size_t pending;
    bool asynchronous;
    pv2_rotation_t rotation;
    size_t repair_pos;
    pv2_copy_ticket_t repair;
};

static bool matches(const pv2_te_composer_t *c, pv2_te_frame_t f)
{
    return c && f.value && c->frame.value == f.value && c->state != IDLE;
}

static bool covered(const pv2_te_composer_t *c, size_t index)
{
    return (c->coverage[index / 8] & (1u << (index % 8))) != 0;
}

static esp_err_t copy(pv2_te_composer_t *c, const pv2_source_t *source,
                      const pv2_rect_t *area, uint32_t x, uint32_t y)
{
    pv2_copy_request_t request = {
        .source = *source, .destination = c->buffers[c->candidate],
        .source_area = *area, .destination_x = x, .destination_y = y,
        .rotation = PV2_ROTATE_0,
    };
    pv2_copy_ticket_t ticket;
    esp_err_t err = pv2_pixel_submit(c->pixel, &request, &ticket);
    if (err != ESP_OK) {
        return err;
    }
    if (c->asynchronous) {
        c->repair = ticket;
        return ESP_ERR_TIMEOUT;
    }
    /* Synchronous compatibility path. */
    pv2_copy_status_t status;
    err = pv2_pixel_wait(c->pixel, ticket, UINT32_MAX, &status);
    esp_err_t release = pv2_pixel_release(c->pixel, ticket);
    return err != ESP_OK ? err : status.result != ESP_OK ? status.result : release;
}

esp_err_t pv2_te_composer_create(const pv2_te_composer_config_t *cfg, pv2_te_composer_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!cfg || !cfg->width || !cfg->height || cfg->rotation < PV2_ROTATE_0 || cfg->rotation > PV2_ROTATE_270 ||
            (cfg->format != PV2_FORMAT_RGB565 && cfg->format != PV2_FORMAT_RGB888) ||
            (cfg->panel_order != PV2_ORDER_NATIVE && cfg->panel_order != PV2_ORDER_SWAPPED) ||
            (cfg->format == PV2_FORMAT_RGB888 && cfg->panel_order != PV2_ORDER_NATIVE)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t bpp = cfg->format == PV2_FORMAT_RGB565 ? 2 : 3;
    if (cfg->width > SIZE_MAX / cfg->height) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t pixels = (size_t)cfg->width * cfg->height;
    if (pixels > (SIZE_MAX - (PV2_BUFFER_ALIGNMENT - 1)) / bpp) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_te_composer_t *c = pv2_alloc(sizeof(*c));
    if (!c) {
        return ESP_ERR_NO_MEM;
    }
    c->baseline = -1;
    c->rotation = cfg->rotation;
    c->asynchronous = cfg->pixel.asynchronous;
    c->coverage_bytes = pixels / 8 + (pixels % 8 != 0);
    c->coverage = pv2_alloc(c->coverage_bytes);
    for (size_t i = 0; i < 2; i++) {
        c->buffers[i] = (pv2_surface_t) {
            .pixels = pv2_alloc_pixels(pixels * bpp), .capacity_bytes = pixels * bpp,
            .layout = {cfg->width, cfg->height, cfg->width * bpp, cfg->format, cfg->panel_order},
        };
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    if (!c->coverage || !c->buffers[0].pixels || !c->buffers[1].pixels) {
        goto fail;
    }
    pv2_pixel_config_t pc = cfg->pixel;
    if (!pc.max_jobs) {
        pc.max_jobs = 1;
    }
    err = pv2_pixel_create(&pc, &c->pixel);
    if (err != ESP_OK) {
        goto fail;
    }
    *out = c;
    return ESP_OK;
fail:
    pv2_free(c->coverage);
    pv2_free(c->buffers[0].pixels);
    pv2_free(c->buffers[1].pixels);
    pv2_free(c);
    return err;
}

esp_err_t pv2_te_composer_begin(pv2_te_composer_t *c, pv2_te_frame_t *out)
{
    if (!c || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_te_frame_t) {
        0
    };
    if (c->state != IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    c->candidate = c->baseline == 0 ? 1 : 0;
    memset(c->coverage, 0, c->coverage_bytes);
    c->frame.value = token;
    c->state = BUILDING;
    c->repair_pos = 0;
    *out = c->frame;
    return ESP_OK;
}

esp_err_t pv2_te_composer_queue_write(pv2_te_composer_t *c, pv2_te_frame_t f,
                                      const pv2_source_t *source, const pv2_rect_t *area, uint32_t x, uint32_t y,
                                      pv2_copy_ticket_t *ticket)
{
    if (!ticket) {
        return ESP_ERR_INVALID_ARG;
    }
    ticket->value = 0;
    if (!matches(c, f) || c->state != BUILDING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!source || !area) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t slot = 0;
    while (slot < PV2_MAX_COPY_JOBS && c->writes[slot].ticket.value) {
        slot++;
    }
    if (slot == PV2_MAX_COPY_JOBS) {
        return ESP_ERR_NO_MEM;
    }
    pv2_copy_request_t r = {.source = *source, .destination = c->buffers[c->candidate],
                            .source_area = *area, .destination_x = x, .destination_y = y, .rotation = c->rotation
                           };
    esp_err_t error = pv2_pixel_submit(c->pixel, &r, ticket);
    if (error != ESP_OK) {
        return error;
    }
    c->writes[slot].ticket = *ticket;
    bool quarter = c->rotation == PV2_ROTATE_90 || c->rotation == PV2_ROTATE_270;
    c->writes[slot].area = (pv2_rect_t) {
        x, y, quarter ? area->height : area->width, quarter ? area->width : area->height
    };
    c->pending++;
    return ESP_OK;
}

esp_err_t pv2_te_composer_finish_write(pv2_te_composer_t *c, pv2_copy_ticket_t ticket,
                                       esp_err_t *result)
{
    if (!c || !ticket.value || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t slot = 0;
    while (slot < PV2_MAX_COPY_JOBS && c->writes[slot].ticket.value != ticket.value) {
        slot++;
    }
    if (slot == PV2_MAX_COPY_JOBS) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_copy_status_t status;
    esp_err_t error = pv2_pixel_wait(c->pixel, ticket, 0, &status);
    if (error != ESP_OK) {
        return error;
    }
    *result = status.result;
    if (status.result == ESP_OK) {
        pv2_rect_t a = c->writes[slot].area;
        size_t width = c->buffers[c->candidate].layout.width;
        for (uint32_t row = 0; row < a.height; row++) {
            pv2_coverage_set(c->coverage, ((size_t)a.y + row) * width + a.x, a.width);
        }
    }
    error = pv2_pixel_release(c->pixel, ticket);
    if (error != ESP_OK) {
        return error;
    }
    c->writes[slot].ticket.value = 0;
    c->pending--;
    return ESP_OK;
}

esp_err_t pv2_te_composer_write(pv2_te_composer_t *c, pv2_te_frame_t f,
                                const pv2_source_t *source, const pv2_rect_t *area, uint32_t x, uint32_t y)
{
    pv2_copy_ticket_t ticket;
    esp_err_t error = pv2_te_composer_queue_write(c, f, source, area, x, y, &ticket);
    if (error != ESP_OK) {
        return error;
    }
    pv2_copy_status_t status;
    error = pv2_pixel_wait(c->pixel, ticket, UINT32_MAX, &status);
    if (error != ESP_OK) {
        return error;
    }
    esp_err_t result;
    error = pv2_te_composer_finish_write(c, ticket, &result);
    return error == ESP_OK ? result : error;
}

esp_err_t pv2_te_composer_stop(pv2_te_composer_t *c, uint32_t timeout)
{
    if (!c) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_pixel_stop(c->pixel, timeout);
    if (error == ESP_OK && c->repair.value) {
        error = pv2_pixel_release(c->pixel, c->repair);
        if (error == ESP_OK) {
            c->repair.value = 0;
        }
    }
    return error;
}

esp_err_t pv2_te_composer_seal(pv2_te_composer_t *c, pv2_te_frame_t f, uint64_t ready_us)
{
    if (!matches(c, f) || (c->state != BUILDING && c->state != REPAIR)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (c->pending) {
        return ESP_ERR_TIMEOUT;
    }
    if (c->repair.value) {
        pv2_copy_status_t status;
        esp_err_t error = pv2_pixel_wait(c->pixel, c->repair, 0, &status);
        if (error != ESP_OK) {
            return error;
        }
        (void)pv2_pixel_release(c->pixel, c->repair);
        c->repair.value = 0;
        if (status.result != ESP_OK) {
            return status.result;
        }
    }
    pv2_surface_t *surface = &c->buffers[c->candidate];
    size_t width = surface->layout.width, count = width * surface->layout.height;
    if (c->baseline < 0) {
        if (!pv2_coverage_full(c->coverage, count)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        c->state = REPAIR;
        pv2_surface_t *base = &c->buffers[c->baseline];
        pv2_source_t source = {base->pixels, base->capacity_bytes, base->layout};
        while (c->repair_pos < count) {
            size_t i = c->repair_pos;
            if (covered(c, i)) {
                c->repair_pos++;
                continue;
            }
            uint32_t x = i % width, y = i / width;
            uint32_t end = x + 1;
            while (end < width && !covered(c, (size_t)y * width + end)) {
                end++;
            }
            pv2_rect_t area = {x, y, end - x, 1};
            esp_err_t error = copy(c, &source, &area, x, y);
            if (error == ESP_OK || error == ESP_ERR_TIMEOUT) {
                c->repair_pos += area.width;
            }
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    c->ready_us = ready_us;
    c->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_offer(pv2_te_composer_t *c, pv2_te_frame_t f,
                                uint64_t edge_us, uint64_t now_us, uint64_t window_us,
                                pv2_source_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_source_t) {
        0
    };
    if (!matches(c, f) || c->state != READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!window_us || now_us < edge_us) {
        return ESP_ERR_INVALID_ARG;
    }
    if (edge_us < c->ready_us || now_us - edge_us >= window_us ||
            (c->edge_seen && edge_us <= c->last_edge_us)) {
        return ESP_ERR_TIMEOUT;
    }
    c->edge_seen = true;
    c->last_edge_us = edge_us;
    pv2_surface_t *s = &c->buffers[c->candidate];
    *out = (pv2_source_t) {
        s->pixels, s->capacity_bytes, s->layout
    };
    c->state = INFLIGHT;
    return ESP_OK;
}

esp_err_t pv2_te_composer_reject(pv2_te_composer_t *c, pv2_te_frame_t f)
{
    if (!matches(c, f) || c->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    c->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_complete(pv2_te_composer_t *c, pv2_te_frame_t f, esp_err_t result)
{
    if (!matches(c, f) || c->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        c->baseline = c->candidate;
    }
    c->state = IDLE;
    c->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_cancel(pv2_te_composer_t *c, pv2_te_frame_t f)
{
    if (!matches(c, f) || c->state == INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (c->pending) {
        return ESP_ERR_TIMEOUT;
    }
    if (c->repair.value) {
        pv2_copy_status_t status;
        esp_err_t error = pv2_pixel_cancel(c->pixel, c->repair, 0, &status);
        if (error != ESP_OK) {
            return error;
        }
        (void)pv2_pixel_release(c->pixel, c->repair);
        c->repair.value = 0;
    }
    c->state = IDLE;
    c->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_destroy(pv2_te_composer_t *c)
{
    if (!c) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->state != IDLE || c->pending) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = pv2_pixel_stop(c->pixel, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = pv2_pixel_destroy(c->pixel);
    if (err != ESP_OK) {
        return err;
    }
    pv2_free(c->coverage);
    pv2_free(c->buffers[0].pixels);
    pv2_free(c->buffers[1].pixels);
    pv2_free(c);
    return ESP_OK;
}

esp_err_t pv2_te_composer_set_rotation(pv2_te_composer_t *c, pv2_rotation_t rotation)
{
    if (!c || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->state != IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    c->rotation = rotation;
    return ESP_OK;
}
