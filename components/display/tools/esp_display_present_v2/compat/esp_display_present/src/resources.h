/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present.h"
#include "present_plan.h"
#include "present_lcd_scanout.h"
/* Private ownership aggregate for the V1 facade; not an application API.
 * Resource task APIs are facade-serialized, including control/handoff calls.
 * Stop/destroy require the producer to be paused; callbacks may run concurrently. */
typedef struct pv2_legacy_resources pv2_legacy_resources_t;
typedef struct {
    pv2_resolved_plan_t plan;
    const char *preferred_executor;
    uint64_t hardware_copies, cpu_copies; /* Executed attempts, including errors. */
    esp_err_t dma2d_create_result, ppa_create_result;
    bool ready, closing;
} pv2_legacy_resource_status_t;
esp_err_t pv2_legacy_plan(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed, pv2_plan_config_t *);
esp_err_t pv2_legacy_resources_create(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed,
                                      pv2_scanout_vsync_fn, void *, pv2_legacy_resources_t **);
pv2_presenter_t *pv2_legacy_resources_presenter(pv2_legacy_resources_t *);
esp_err_t pv2_legacy_resources_prepare(pv2_legacy_resources_t *, uint32_t timeout_ms);
esp_err_t pv2_legacy_resources_select_executor(pv2_legacy_resources_t *, pv2_rotation_t);
esp_err_t pv2_legacy_resources_status(pv2_legacy_resources_t *, pv2_legacy_resource_status_t *);
esp_err_t pv2_legacy_resources_destroy(pv2_legacy_resources_t *, uint32_t timeout_ms);

esp_err_t pv2_legacy_resources_stop(pv2_legacy_resources_t *, uint32_t timeout_ms);

/* Drains native work, retains driver pool and logical shape like V1. */
esp_err_t pv2_legacy_resources_set_rotation(pv2_legacy_resources_t *, pv2_rotation_t, uint32_t);
