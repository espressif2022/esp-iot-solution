/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_pixel_accel.h"
#include "present_executor.h"
#include "present_os.h"
struct pv2_pixel_accel {
    pv2_executor_dispatch_t dispatch;
};
esp_err_t pv2_pixel_accel_create(const pv2_pixel_accel_config_t *config, pv2_pixel_accel_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || config->kind < PV2_ACCEL_DMA2D || config->kind > PV2_ACCEL_PPA) {
        return ESP_ERR_INVALID_ARG;
    }
    pv2_pixel_accel_t *a = pv2_alloc(sizeof(*a));
    if (!a) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = ESP_OK;
    /* Allocate CPU first: no fallible hardware teardown on later allocation failure. */
    if (config->cpu_fallback) {
        error = pv2_executor_cpu_create(&a->dispatch.fallback);
    }
    if (error == ESP_OK) {
        error = config->kind == PV2_ACCEL_PPA ? pv2_executor_ppa_create(&a->dispatch.primary)
                : pv2_executor_dma2d_create(&a->dispatch.primary);
    }
    if (error != ESP_OK) {
        if (a->dispatch.fallback) {
            (void)pv2_executor_destroy(a->dispatch.fallback);
        }
        pv2_free(a);
        return error;
    }
    *out = a;
    return ESP_OK;
}
esp_err_t pv2_pixel_accel_execute(void *context, const pv2_copy_request_t *request)
{
    pv2_pixel_accel_t *a = context;
    return a ? pv2_executor_dispatch_execute(&a->dispatch, request) : ESP_ERR_INVALID_ARG;
}
esp_err_t pv2_pixel_accel_destroy(pv2_pixel_accel_t *a)
{
    if (!a) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = pv2_executor_destroy(a->dispatch.primary);
    if (error != ESP_OK) {
        return error;
    }
    if (a->dispatch.fallback) {
        (void)pv2_executor_destroy(a->dispatch.fallback);
    }
    pv2_free(a);
    return ESP_OK;
}
