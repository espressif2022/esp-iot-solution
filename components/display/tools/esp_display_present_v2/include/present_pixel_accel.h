/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_pixel_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Compatibility facade. New code may inject present_executor.h instances to
 * query capabilities/probe before choosing a path. CPU fallback is selected by
 * the common dispatcher, never inside a hardware plugin. */
typedef struct pv2_pixel_accel pv2_pixel_accel_t;
typedef enum { PV2_ACCEL_DMA2D, PV2_ACCEL_PPA } pv2_accel_kind_t;
typedef struct {
    pv2_accel_kind_t kind;
    bool cpu_fallback;
} pv2_pixel_accel_config_t;

/**
 * @brief Local IDF 6.2 platform bridge. DMA2D uses its private 2D copy helper: keep
 * this implementation version-pinned and re-audit on IDF upgrades.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_accel Output accel.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_pixel_accel_create(const pv2_pixel_accel_config_t *config, pv2_pixel_accel_t **out_accel);

/**
 * @brief Assign as pixel.execute/backend.copy_execute. Blocks only its worker until
 * hardware really completes. No timeout can turn active DMA into a terminal
 * ticket. Pixel stop may timeout and retain the engine while this drains.
 * DMA2D: no rotation/swap; PPA: rotation/swap. Alignment/layout restrictions
 * select CPU fallback only BEFORE hardware submission. Never retry on CPU after
 * a hardware error. C2M preserves destination dirty lines before DMA; M2C is
 * aligned and restricted to the exclusively borrowed allocation.
 *
 * @param context Borrowed implementation context.
 * @param request Request descriptor, subject to the ownership contract above.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_pixel_accel_execute(void *context, const pv2_copy_request_t *request);

/**
 * @brief Call after successful stop/join of every pixel engine using this executor;
 * destroy/retry is owner-serialized. Frees only after driver unregister succeeds.
 *
 * @param accel Accelerator instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t pv2_pixel_accel_destroy(pv2_pixel_accel_t *accel);
#ifdef __cplusplus
}
#endif
