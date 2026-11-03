/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present.h"
#include "present_plan.h"
#include "present_lcd_scanout.h"
/* Private hardware resource aggregate for esp_display_presenter_t.
 * Resource task APIs are caller-serialized, including control/handoff calls.
 * Stop/destroy require the producer to be paused; callbacks may run concurrently. */
typedef struct present_resources present_resources_t;
typedef struct {
    esp_display_present_resolved_plan_t plan;
    const char *preferred_executor;
    uint64_t hardware_copies, cpu_copies; /* Executed attempts, including errors. */
    esp_err_t dma2d_create_result, ppa_create_result;
    bool ready, closing;
} present_resource_status_t;
esp_err_t present_resources_plan(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed, esp_display_present_plan_config_t *);
esp_err_t present_resources_create(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed,
                                   esp_display_present_scanout_vsync_fn, void *, present_resources_t **);
esp_display_present_engine_t *present_resources_presenter(present_resources_t *);
esp_err_t present_resources_prepare(present_resources_t *, uint32_t timeout_ms);
esp_err_t present_resources_select_executor(present_resources_t *, esp_display_present_engine_rotation_t);
esp_err_t present_resources_status(present_resources_t *, present_resource_status_t *);
esp_err_t present_resources_destroy(present_resources_t *, uint32_t timeout_ms);

esp_err_t present_resources_stop(present_resources_t *, uint32_t timeout_ms);

/* Drains native work, retains driver pool and logical shape. */
esp_err_t present_resources_set_rotation(present_resources_t *, esp_display_present_engine_rotation_t, uint32_t);
