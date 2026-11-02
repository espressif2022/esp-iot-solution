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

static compose_job_t *find_job(pv2_te_composer_t *composer, pv2_te_frame_t f)
{
    if (!composer || !f.value) {
        return NULL;
    }
    for (size_t i = 0; i < composer->image_count; i++)
        if (composer->jobs[i].state != IDLE && composer->jobs[i].frame.value == f.value) {
            return &composer->jobs[i];
        }
    return NULL;
}

static esp_err_t copy(pv2_te_composer_t *composer, compose_job_t *job, const pv2_source_t *source,
                      const pv2_rect_t *area, uint32_t x, uint32_t y)
{
    pv2_copy_request_t request = {
        .source = *source, .destination = composer->buffers[job->candidate],
        .source_area = *area, .destination_x = x, .destination_y = y,
        .rotation = PV2_ROTATE_0,
    };
    pv2_copy_ticket_t ticket;
    esp_err_t err = pv2_pixel_submit(composer->pixel, &request, &ticket);
    if (err != ESP_OK) {
        return err;
    }
    if (composer->asynchronous) {
        composer->repair = ticket;
        return ESP_ERR_TIMEOUT;
    }
    /* Synchronous compatibility path. */
    pv2_copy_status_t status;
    err = pv2_pixel_wait(composer->pixel, ticket, UINT32_MAX, &status);
    esp_err_t release = pv2_pixel_release(composer->pixel, ticket);
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
    pv2_te_composer_t *composer = pv2_alloc(sizeof(*composer));
    if (!composer) {
        return ESP_ERR_NO_MEM;
    }
    composer->baseline = -1;
    composer->image_count = cfg->image_count ? cfg->image_count : 2;
    composer->pipelined = cfg->image_count != 0;
    composer->rotation = cfg->rotation;
    composer->asynchronous = cfg->pixel.asynchronous;
    esp_err_t image_error = pv2_image_init(&composer->image, cfg->width, cfg->height);
    for (size_t i = 0; i < composer->image_count; i++) {
        composer->buffers[i] = (pv2_surface_t) {
            .pixels = pv2_alloc_pixels_in(pixels * bpp, cfg->image_in_psram), .capacity_bytes = pixels * bpp,
            .layout = {cfg->width, cfg->height, cfg->width * bpp, cfg->format, cfg->panel_order},
        };
    }
    esp_err_t err = ESP_ERR_NO_MEM;
    if (image_error != ESP_OK || !composer->buffers[0].pixels || (composer->image_count == 2 && !composer->buffers[1].pixels)) {
        goto fail;
    }
    pv2_pixel_config_t pc = cfg->pixel;
    if (!pc.max_jobs) {
        pc.max_jobs = 1;
    }
    err = pv2_pixel_create(&pc, &composer->pixel);
    if (err != ESP_OK) {
        goto fail;
    }
    *out = composer;
    return ESP_OK;
fail:
    pv2_image_deinit(&composer->image);
    pv2_free(composer->buffers[0].pixels);
    pv2_free(composer->buffers[1].pixels);
    pv2_free(composer);
    return err;
}

esp_err_t pv2_te_composer_begin(pv2_te_composer_t *composer, pv2_te_frame_t *out)
{
    if (!composer || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_te_frame_t) {
        0
    };
    compose_job_t *job = NULL;
    for (size_t i = 0; i < composer->image_count; i++) {
        if (!composer->pipelined && composer->jobs[i].state != IDLE) {
            return ESP_ERR_INVALID_STATE;
        }
        if (composer->jobs[i].state == BUILDING || composer->jobs[i].state == REPAIR) {
            return ESP_ERR_INVALID_STATE;
        }
        if (composer->jobs[i].state == IDLE && (composer->image_count == 1 || i != (size_t)composer->baseline)) {
            job = &composer->jobs[i];
        }
    }
    if (!job) {
        return ESP_ERR_NO_MEM;
    }
    uint64_t token = pv2_new_token();
    if (!token) {
        return ESP_ERR_NO_MEM;
    }
    size_t candidate = (size_t)(job - composer->jobs);
    *job = (compose_job_t) {
        .state = BUILDING, .frame = {token}, .candidate = candidate
    };
    esp_err_t error = pv2_image_begin(&composer->image, candidate, composer->baseline < 0 ? SIZE_MAX : (size_t)composer->baseline);
    if (error != ESP_OK) {
        job->state = IDLE;
        return error;
    }
    composer->repair_pos = 0;
    *out = job->frame;
    return ESP_OK;
}

esp_err_t pv2_te_composer_queue_write(pv2_te_composer_t *composer, pv2_te_frame_t f,
                                      const pv2_source_t *source, const pv2_rect_t *area, uint32_t x, uint32_t y,
                                      pv2_copy_ticket_t *ticket)
{
    compose_job_t *job = find_job(composer, f);
    if (!ticket) {
        return ESP_ERR_INVALID_ARG;
    }
    ticket->value = 0;
    if (!job || job->state != BUILDING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!source || !area) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t slot = 0;
    while (slot < PV2_MAX_COPY_JOBS && composer->writes[slot].ticket.value) {
        slot++;
    }
    if (slot == PV2_MAX_COPY_JOBS) {
        return ESP_ERR_NO_MEM;
    }
    pv2_copy_request_t r = {.source = *source, .destination = composer->buffers[job->candidate],
                            .source_area = *area, .destination_x = x, .destination_y = y, .rotation = composer->rotation
                           };
    esp_err_t error = pv2_pixel_submit(composer->pixel, &r, ticket);
    if (error != ESP_OK) {
        return error;
    }
    composer->writes[slot].ticket = *ticket;
    bool quarter = composer->rotation == PV2_ROTATE_90 || composer->rotation == PV2_ROTATE_270;
    composer->writes[slot].area = (pv2_rect_t) {
        x, y, quarter ? area->height : area->width, quarter ? area->width : area->height
    };
    composer->pending++;
    return ESP_OK;
}

esp_err_t pv2_te_composer_finish_write(pv2_te_composer_t *composer, pv2_copy_ticket_t ticket,
                                       esp_err_t *result)
{
    if (!composer || !ticket.value || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t slot = 0;
    while (slot < PV2_MAX_COPY_JOBS && composer->writes[slot].ticket.value != ticket.value) {
        slot++;
    }
    if (slot == PV2_MAX_COPY_JOBS) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_copy_status_t status;
    esp_err_t error = pv2_pixel_wait(composer->pixel, ticket, 0, &status);
    if (error != ESP_OK) {
        return error;
    }
    *result = status.result;
    if (status.result == ESP_OK) {
        pv2_rect_t a = composer->writes[slot].area;
        *result = pv2_image_cover(&composer->image, a);
    }
    error = pv2_pixel_release(composer->pixel, ticket);
    if (error != ESP_OK) {
        return error;
    }
    composer->writes[slot].ticket.value = 0;
    composer->pending--;
    return ESP_OK;
}

esp_err_t pv2_te_composer_write(pv2_te_composer_t *composer, pv2_te_frame_t f,
                                const pv2_source_t *source, const pv2_rect_t *area, uint32_t x, uint32_t y)
{
    pv2_copy_ticket_t ticket;
    esp_err_t error = pv2_te_composer_queue_write(composer, f, source, area, x, y, &ticket);
    if (error != ESP_OK) {
        return error;
    }
    pv2_copy_status_t status;
    error = pv2_pixel_wait(composer->pixel, ticket, UINT32_MAX, &status);
    if (error != ESP_OK) {
        return error;
    }
    esp_err_t result;
    error = pv2_te_composer_finish_write(composer, ticket, &result);
    return error == ESP_OK ? result : error;
}

esp_err_t pv2_te_composer_stop(pv2_te_composer_t *composer, uint32_t timeout)
{
    if (!composer) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_pixel_stop(composer->pixel, timeout);
    if (error == ESP_OK && composer->repair.value) {
        error = pv2_pixel_release(composer->pixel, composer->repair);
        if (error == ESP_OK) {
            composer->repair.value = 0;
        }
    }
    return error;
}

esp_err_t pv2_te_composer_seal(pv2_te_composer_t *composer, pv2_te_frame_t f, uint64_t ready_us)
{
    compose_job_t *job = find_job(composer, f);
    if (!job || (job->state != BUILDING && job->state != REPAIR)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (composer->pending) {
        return ESP_ERR_TIMEOUT;
    }
    if (composer->repair.value) {
        pv2_copy_status_t status;
        esp_err_t error = pv2_pixel_wait(composer->pixel, composer->repair, 0, &status);
        if (error != ESP_OK) {
            return error;
        }
        (void)pv2_pixel_release(composer->pixel, composer->repair);
        composer->repair.value = 0;
        if (status.result != ESP_OK) {
            return status.result;
        }
    }
    pv2_surface_t *surface = &composer->buffers[job->candidate];
    (void)surface;
    if (composer->baseline < 0) {
        if (!pv2_image_full(&composer->image)) {
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        job->state = REPAIR;
        pv2_surface_t *base = &composer->buffers[composer->baseline];
        pv2_source_t source = {base->pixels, base->capacity_bytes, base->layout};
        pv2_image_prepare_repair(&composer->image, false);
        pv2_rect_t area;
        while (pv2_image_next_repair(&composer->image, &area)) {
            esp_err_t error = copy(composer, job, &source, &area, area.x, area.y);
            if (error == ESP_OK || error == ESP_ERR_TIMEOUT) {
                pv2_image_repaired(&composer->image, &area);
            }
            if (error != ESP_OK) {
                return error;
            }
        }
    }
    if (composer->image.coverage_error != ESP_OK) {
        return composer->image.coverage_error;
    }
    if (composer->pipelined) {
        esp_err_t commit = pv2_image_commit(&composer->image);
        if (commit != ESP_OK) {
            return commit;
        }
        composer->baseline = job->candidate;
    }
    if (!composer->pipelined) {
        pv2_allocation_written(&composer->image.allocations[job->candidate]);
    }
    job->ready_us = ready_us;
    job->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_offer(pv2_te_composer_t *composer, pv2_te_frame_t f,
                                uint64_t edge_us, uint64_t now_us, uint64_t window_us,
                                pv2_source_t *out)
{
    compose_job_t *job = find_job(composer, f);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (pv2_source_t) {
        0
    };
    if (!job || job->state != READY) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!window_us || now_us < edge_us) {
        return ESP_ERR_INVALID_ARG;
    }
    if (edge_us < job->ready_us || now_us - edge_us >= window_us ||
            (job->edge_seen && edge_us <= job->last_edge_us)) {
        return ESP_ERR_TIMEOUT;
    }
    job->edge_seen = true;
    job->last_edge_us = edge_us;
    return pv2_te_composer_pin(composer, f, out);
}
esp_err_t pv2_te_composer_pin(pv2_te_composer_t *composer, pv2_te_frame_t f, pv2_source_t *out)
{
    compose_job_t *job = find_job(composer, f);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!job || job->state != READY) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_surface_t *surface = &composer->buffers[job->candidate];
    *out = (pv2_source_t) {
        surface->pixels, surface->capacity_bytes, surface->layout
    };
    pv2_allocation_hold(&composer->image.allocations[job->candidate], PV2_REF_TRANSFER);
    job->state = INFLIGHT;
    return ESP_OK;
}

esp_err_t pv2_te_composer_reject(pv2_te_composer_t *composer, pv2_te_frame_t f)
{
    compose_job_t *job = find_job(composer, f);
    if (!job || job->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_allocation_drop(&composer->image.allocations[job->candidate], PV2_REF_TRANSFER);
    job->state = READY;
    return ESP_OK;
}

esp_err_t pv2_te_composer_complete(pv2_te_composer_t *composer, pv2_te_frame_t f, esp_err_t result)
{
    compose_job_t *job = find_job(composer, f);
    if (!job || job->state != INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_allocation_drop(&composer->image.allocations[job->candidate], PV2_REF_TRANSFER);
    if (!composer->pipelined) {
        if (result == ESP_OK) {
            composer->image.allocations[job->candidate].writer = true; /* metadata publication after a drained legacy transfer */
            esp_err_t error = pv2_image_commit(&composer->image);
            if (error != ESP_OK) {
                return error;
            }
            composer->baseline = job->candidate;
        } else {
            pv2_image_cancel(&composer->image);
        }
    }
    job->state = IDLE;
    job->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_cancel(pv2_te_composer_t *composer, pv2_te_frame_t f)
{
    compose_job_t *job = find_job(composer, f);
    if (!job || job->state == INFLIGHT) {
        return ESP_ERR_INVALID_STATE;
    }
    if (composer->pending) {
        return ESP_ERR_TIMEOUT;
    }
    if (composer->repair.value) {
        pv2_copy_status_t status;
        esp_err_t error = pv2_pixel_cancel(composer->pixel, composer->repair, 0, &status);
        if (error != ESP_OK) {
            return error;
        }
        (void)pv2_pixel_release(composer->pixel, composer->repair);
        composer->repair.value = 0;
    }
    if (job->state == BUILDING || job->state == REPAIR || !composer->pipelined) {
        pv2_image_cancel(&composer->image);
    }
    composer->image.versions[job->candidate] = 0;
    if (composer->baseline == (int)job->candidate) {
        composer->baseline = -1;
    }
    job->state = IDLE;
    job->frame.value = 0;
    return ESP_OK;
}

esp_err_t pv2_te_composer_destroy(pv2_te_composer_t *composer)
{
    if (!composer) {
        return ESP_ERR_INVALID_ARG;
    }
    if (composer->jobs[0].state != IDLE || composer->jobs[1].state != IDLE || composer->pending) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = pv2_pixel_stop(composer->pixel, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = pv2_pixel_destroy(composer->pixel);
    if (err != ESP_OK) {
        return err;
    }
    pv2_image_deinit(&composer->image);
    pv2_free(composer->buffers[0].pixels);
    pv2_free(composer->buffers[1].pixels);
    pv2_free(composer);
    return ESP_OK;
}

esp_err_t pv2_te_composer_reorient(pv2_te_composer_t *composer, pv2_rotation_t rotation, uint32_t width, uint32_t height)
{
    if (!composer || rotation < PV2_ROTATE_0 || rotation > PV2_ROTATE_270) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!width || !height || (uint64_t)width * height != (uint64_t)composer->image.width * composer->image.height) {
        return ESP_ERR_INVALID_ARG;
    }
    if (composer->jobs[0].state != IDLE || composer->jobs[1].state != IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    composer->rotation = rotation;
    for (size_t i = 0; i < composer->image_count; ++i) {
        composer->buffers[i].layout.width = width;
        composer->buffers[i].layout.height = height;
        composer->buffers[i].layout.stride_bytes = (size_t)width * (composer->buffers[i].layout.format == PV2_FORMAT_RGB565 ? 2 : 3);
    }
    pv2_image_reset(&composer->image, width, height);
    composer->baseline = -1;
    return ESP_OK;
}

esp_err_t pv2_te_composer_set_rotation(pv2_te_composer_t *composer, pv2_rotation_t rotation)
{
    return composer ? pv2_te_composer_reorient(composer, rotation, composer->image.width, composer->image.height) : ESP_ERR_INVALID_ARG;
}

size_t pv2_composer_memory(void)
{
    return sizeof(pv2_te_composer_t);
}
