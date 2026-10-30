/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_executor.h"
#include "present_os.h"
typedef struct {
    pv2_executor_t base;
    pv2_mutex_t *gate;
} cpu_t;
static esp_err_t caps(void *context, pv2_executor_caps_t *out)
{
    (void)context;
    *out = (pv2_executor_caps_t) {
        .name = "cpu", .format_mask = 3, .rotation_mask = 15,
        .byte_swap = true, .in_place = true, .base_alignment = 1,
        .destination_capacity_alignment = 1, .stride_alignment_bytes = 1
    };
    return ESP_OK;
}
static esp_err_t probe(void *context, const pv2_copy_request_t *r, pv2_executor_reason_t *reason)
{
    (void)context;
    esp_err_t error = pv2_pixel_validate_request(r);
    *reason = error == ESP_OK ? PV2_EXEC_SUPPORTED : PV2_EXEC_INVALID_REQUEST;
    return error;
}
static esp_err_t execute(void *context, const pv2_copy_request_t *r)
{
    cpu_t *c = context;
    (void)pv2_mutex_take(c->gate, UINT32_MAX);
    esp_err_t error = pv2_pixel_execute_cpu(r);
    pv2_mutex_give(c->gate);
    return error;
}
static esp_err_t destroy(void *context)
{
    cpu_t *c = context;
    pv2_mutex_destroy(c->gate);
    pv2_free(c);
    return ESP_OK;
}
static const pv2_executor_ops_t ops = {caps, probe, execute, destroy};
esp_err_t pv2_executor_cpu_create(pv2_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    cpu_t *c = pv2_alloc(sizeof(*c));
    if (!c) {
        return ESP_ERR_NO_MEM;
    }
    c->gate = pv2_mutex_create();
    if (!c->gate) {
        pv2_free(c);
        return ESP_ERR_NO_MEM;
    }
    c->base = (pv2_executor_t) {
        &ops, c
    };
    *out = &c->base;
    return ESP_OK;
}
