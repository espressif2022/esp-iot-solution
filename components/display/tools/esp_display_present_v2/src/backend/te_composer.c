/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "te_composer.h"
#include "present_os.h"
#include "present_limits.h"
#include "present_image.h"
#include <string.h>

typedef enum { IDLE, BUILDING, REPAIR, READY, INFLIGHT } compose_state_t;
typedef struct {
    compose_state_t state;
    pv2_te_frame_t frame;
    uint64_t ready_us, last_edge_us;
    bool edge_seen;
    size_t candidate;
} compose_job_t;

struct pv2_te_composer {
    pv2_pixel_engine_t *pixel;
    pv2_surface_t buffers[2];
    pv2_image_builder_t image;
    int baseline;
    size_t image_count;
    bool pipelined;
    compose_job_t jobs[2];
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

static compose_job_t *find_job(pv2_te_composer_t *c, pv2_te_frame_t f)
{
    if (!c || !f.value) {
        return NULL;
    }
    for (size_t i = 0; i < c->image_count; i++)
        if (c->jobs[i].state != IDLE && c->jobs[i].frame.value == f.value) {
            return &c->jobs[i];
        }
    return NULL;
}

static esp_err_t copy(pv2_te_composer_t *c, compose_job_t *j, const pv2_source_t *source,
                      const pv2_rect_t *area, uint32_t x, uint32_t y)
{
    pv2_copy_request_t request = {
        .source = *source, .destination = c->buffers[j->candidate],
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
    if (!cfg || cfg->image_count > 2 || !cfg->width || !cfg->height || cfg->rotation < PV2_ROTATE_0 || cfg->rotation > PV2_ROTATE_270 ||
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
    c->image_count = cfg->image_count ? cfg->image_count : 2;
    c->pipelined = cfg->image_count != 0;
    c->rotation = cfg->rotation;
    c->asynchronous = cfg->pixel.asynchronous;
    esp_err_t image_error = pv2_image_init(&c->image, cfg->width, cfg->height);
    for (size_t i = 0; i < c->image_count; i++) {
        c->buffers[i] = (pv2_surface_t) {
            .pixels = pv2_alloc_pixels_in(pixels * bpp, cfg->image_in_psram), .capacity_bytes = pixels * bpp,
            .layout = {cfg->width, cfg->height, cfg->width * bpp, cfg->format, cfg->panel_order},
        };
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    if (image_error != ESP_OK || !c->buffers[0].pixels || (c->image_count == 2 && !c->buffers[1].pixels)) {
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
    pv2_image_deinit(&c->image);
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
    compose_job_t *j = NULL;
    for (size_t i = 0; i < c->image_count; i++) {
        if (!c->pipelined && c->jobs[i].state != IDLE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (c->jobs[i].state == BUILDING || c->jobs[i].state == REPAIR) {
            return ESP_ERR_INVALID_STATE;
        }
        if (c->jobs[i].state == IDLE && (c->image_count == 1 || i != (size_t)c->baseline)) {
            j = &c->jobs[i];
        }
    }
    if (!j) {
        return ESP_ERR_NO_MEM;
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    size_t candidate = (size_t)(j - c->jobs);
    *j = (compose_job_t) {
        .state = BUILDING, .frame = {token}, .candidate = candidate
    };
    esp_err_t error = pv2_image_begin(&c->image, candidate, c->baseline < 0 ? SIZE_MAX : (size_t)c->baseline);
    if (error != ESP_OK) {
        j->state = IDLE;
        return error;
    }
    c->repair_pos = 0;
    *out = j->frame;
    return ESP_OK;
}

esp_err_t pv2_te_composer_queue_write(pv2_te_composer_t *c, pv2_te_frame_t f,
                                      const pv2_source_t *source, const pv2_rect_t *area, uint32_t x, uint32_t y,
                                      pv2_copy_ticket_t *ticket)
{
    compose_job_t *j = find_job(c, f);
    if (!ticket) {
        return ESP_ERR_INVALID_ARG;
    }
    ticket->value = 0;
    if (!j || j->state != BUILDING) {
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
    pv2_copy_request_t r = {.source = *source, .destination = c->buffers[j->candidate],
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
        pv2_image_cover(&c->image, a);
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
    compose_job_t *j = find_job(c, f);
    if (!j || (j->state != BUILDING && j->state != REPAIR)) {
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
    pv2_surface_t *surface = &c->buffers[j->candidate];
    (void)surface;
    if (c->baseline < 0) {
        if (!pv2_image_full(&c->image)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        j->state = REPAIR;
        pv2_surface_t *base = &c->buffers[c->baseline];
        pv2_source_t source = {base->pixels, base->capacity_bytes, base->layout};
        pv2_image_prepare_repair(&c->image, false);
        pv2_rect_t area;
        while (pv2_image_next_repair(&c->image, &area)) {
            esp_err_t error = copy(c, j, &source, &area, area.x, area.y);
            if (error == ESP_OK || error == ESP_ERR_TIMEOUT) {
                pv2_image_repaired(&c->image, &area);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    if (c->pipelined) {
        esp_err_t commit = pv2_image_commit(&c->image);
        if (commit != ESP_OK) {
            return commit;
        }
        c->baseline = j->candidate;
    }
    if (!c->pipelined) {
        pv2_allocation_written(&c->image.allocations[j->candidate]);
    }
    j->ready_us = ready_us;
    j->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_offer(pv2_te_composer_t *c, pv2_te_frame_t f,
                                uint64_t edge_us, uint64_t now_us, uint64_t window_us,
                                pv2_source_t *out)
{
    compose_job_t *j = find_job(c, f);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_source_t) {
        0
    };
    if (!j || j->state != READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!window_us || now_us < edge_us) {
        return ESP_ERR_INVALID_ARG;
    }
    if (edge_us < j->ready_us || now_us - edge_us >= window_us ||
            (j->edge_seen && edge_us <= j->last_edge_us)) {
        return ESP_ERR_TIMEOUT;
    }
    j->edge_seen = true;
    j->last_edge_us = edge_us;
    return pv2_te_composer_pin(c, f, out);
}
esp_err_t pv2_te_composer_pin(pv2_te_composer_t *c, pv2_te_frame_t f, pv2_source_t *out)
{
    compose_job_t *j = find_job(c, f);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!j || j->state != READY) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_surface_t *s = &c->buffers[j->candidate];
    *out = (pv2_source_t) {
        s->pixels, s->capacity_bytes, s->layout
    };
    pv2_allocation_hold(&c->image.allocations[j->candidate], PV2_REF_TRANSFER);
    j->state = INFLIGHT;
    return ESP_OK;
}

esp_err_t pv2_te_composer_reject(pv2_te_composer_t *c, pv2_te_frame_t f)
{
    compose_job_t *j = find_job(c, f);
    if (!j || j->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_allocation_drop(&c->image.allocations[j->candidate], PV2_REF_TRANSFER);
    j->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_complete(pv2_te_composer_t *c, pv2_te_frame_t f, esp_err_t result)
{
    compose_job_t *j = find_job(c, f);
    if (!j || j->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_allocation_drop(&c->image.allocations[j->candidate], PV2_REF_TRANSFER);
    if (!c->pipelined) {
        if (result == ESP_OK) {
            c->image.allocations[j->candidate].writer = true; /* metadata publication after a drained legacy transfer */
            esp_err_t error = pv2_image_commit(&c->image);
            if (error != ESP_OK) {
                return error;
            }
            c->baseline = j->candidate;
        } else {
            pv2_image_cancel(&c->image);
        }
    }
    j->state = IDLE;
    j->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_cancel(pv2_te_composer_t *c, pv2_te_frame_t f)
{
    compose_job_t *j = find_job(c, f);
    if (!j || j->state == INFLIGHT) {
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
    if (j->state == BUILDING || j->state == REPAIR || !c->pipelined) {
        pv2_image_cancel(&c->image);
    }
    c->image.versions[j->candidate] = 0;
    if (c->baseline == (int)j->candidate) {
        c->baseline = -1;
    }
    j->state = IDLE;
    j->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_destroy(pv2_te_composer_t *c)
{
    if (!c) {
        return ESP_ERR_INVALID_ARG;
    }
    if (c->jobs[0].state != IDLE || c->jobs[1].state != IDLE || c->pending) {
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
    pv2_image_deinit(&c->image);
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
    if (c->jobs[0].state != IDLE || c->jobs[1].state != IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    c->rotation = rotation;
    pv2_image_reset(&c->image, c->image.width, c->image.height);
    c->baseline = -1;
    return ESP_OK;
}

size_t pv2_composer_memory(void)
{
    return sizeof(pv2_te_composer_t);
}
