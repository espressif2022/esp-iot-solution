/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_pixel_accel.h"
#include "present_executor.h"
#include "present_os.h"
struct esp_display_present_pixel_accel {
    esp_display_present_executor_dispatch_t dispatch;
};
esp_err_t esp_display_present_pixel_accel_create(const esp_display_present_pixel_accel_config_t *config, esp_display_present_pixel_accel_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || config->kind < ESP_DISPLAY_PRESENT_ACCEL_DMA2D || config->kind > ESP_DISPLAY_PRESENT_ACCEL_PPA) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_display_present_pixel_accel_t *accel = present_alloc(sizeof(*accel));
    if (!accel) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = ESP_OK;
    /* Allocate CPU first: no fallible hardware teardown on later allocation failure. */
    if (config->cpu_fallback) {
        error = esp_display_present_executor_cpu_create(&accel->dispatch.fallback);
    }
    if (error == ESP_OK) {
        error = config->kind == ESP_DISPLAY_PRESENT_ACCEL_PPA ? esp_display_present_executor_ppa_create(&accel->dispatch.primary)
                : esp_display_present_executor_dma2d_create(&accel->dispatch.primary);
    }
    if (error != ESP_OK) {
        if (accel->dispatch.fallback) {
            (void)esp_display_present_executor_destroy(accel->dispatch.fallback);
        }
        present_free(accel);
        return error;
    }
    *out = accel;
    return ESP_OK;
}
esp_err_t esp_display_present_pixel_accel_execute(void *context, const esp_display_present_copy_request_t *request)
{
    esp_display_present_pixel_accel_t *accel = context;
    return accel ? esp_display_present_executor_dispatch_execute(&accel->dispatch, request) : ESP_ERR_INVALID_ARG;
}
esp_err_t esp_display_present_pixel_accel_destroy(esp_display_present_pixel_accel_t *accel)
{
    if (!accel) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t error = esp_display_present_executor_destroy(accel->dispatch.primary);
    if (error != ESP_OK) {
        return error;
    }
    if (accel->dispatch.fallback) {
        (void)esp_display_present_executor_destroy(accel->dispatch.fallback);
    }
    present_free(accel);
    return ESP_OK;
}
