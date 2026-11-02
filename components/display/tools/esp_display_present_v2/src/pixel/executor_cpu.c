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
static esp_err_t probe(void *context, const pv2_copy_request_t *request, pv2_executor_reason_t *reason)
{
    (void)context;
    esp_err_t error = pv2_pixel_validate_request(request);
    *reason = error == ESP_OK ? PV2_EXEC_SUPPORTED : PV2_EXEC_INVALID_REQUEST;
    return error;
}
static esp_err_t execute(void *context, const pv2_copy_request_t *request)
{
    cpu_t *cpu = context;
    (void)pv2_mutex_take(cpu->gate, UINT32_MAX);
    esp_err_t error = pv2_pixel_execute_cpu(request);
    pv2_mutex_give(cpu->gate);
    return error;
}
static esp_err_t destroy(void *context)
{
    cpu_t *cpu = context;
    pv2_mutex_destroy(cpu->gate);
    pv2_free(cpu);
    return ESP_OK;
}
static const pv2_executor_ops_t ops = {caps, probe, execute, destroy};
esp_err_t pv2_executor_cpu_create(pv2_executor_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    cpu_t *cpu = pv2_alloc(sizeof(*cpu));
    if (!cpu) {
        return ESP_ERR_NO_MEM;
    }
    cpu->gate = pv2_mutex_create();
    if (!cpu->gate) {
        pv2_free(cpu);
        return ESP_ERR_NO_MEM;
    }
    cpu->base = (pv2_executor_t) {
        &ops, cpu
    };
    *out = &cpu->base;
    return ESP_OK;
}
