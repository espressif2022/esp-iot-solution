/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present.h"
#include "config/present_plan.h"
#include "platform/lcd/present_lcd_fb_scanout.h"
/**
 * @brief Private hardware resource aggregate for esp_display_presenter_t.
 *
 * Resource task APIs are caller-serialized, including control/handoff calls.
 * Stop/destroy require the producer to be paused; callbacks may run concurrently.
 */
typedef struct present_resources present_resources_t;

/** @brief Creation-time allocation plan and runtime copy/executor telemetry. */
typedef struct {
    esp_display_present_resolved_plan_t plan; /**< Creation-time plan snapshot (query caps for rotated dimensions). */
    uint32_t degradation_flags;               /**< ESP_DISPLAY_PRESENT_DEGRADE_* bitmask applied at creation. */
    const char *preferred_executor;           /**< Selected executor name. */
    uint64_t hardware_copies;                 /**< Executed hardware-executor copy attempts (including errors). */
    uint64_t cpu_copies;                      /**< Executed CPU-executor copy attempts (including errors). */
    esp_err_t dma2d_create_result;            /**< Result of DMA2D executor probe/creation. */
    esp_err_t ppa_create_result;              /**< Result of PPA executor probe/creation. */
    bool ready;                               /**< True once resources and engine are ready. */
    bool closing;                             /**< True once stop/teardown has started. */
} present_resource_status_t;

/** @brief Build a resource plan from public presenter config without allocating hardware. */
esp_err_t present_resources_plan(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed, esp_display_present_plan_config_t *);
/** @brief Allocate hardware adapters, executors, and presentation engine from config. */
esp_err_t present_resources_create(const esp_display_presenter_config_t *, bool rgb_bounce_confirmed,
                                   esp_display_present_scanout_vsync_fn, void *, present_resources_t **);
/** @brief Return the owned presentation engine instance. */
esp_display_present_engine_t *present_resources_presenter(present_resources_t *);
/** @brief Prepare initial scanout fence or TE synchronization before the first frame. */
esp_err_t present_resources_prepare(present_resources_t *, uint32_t timeout_ms);
/** @brief Select/create hardware and fallback executors for the given rotation. */
esp_err_t present_resources_select_executor(present_resources_t *, esp_display_present_engine_rotation_t);
/** @brief Return immutable creation-time effective mode and degradation flags. */
void present_resources_get_selection(const present_resources_t *, esp_display_present_mode_t *, uint32_t *);
/** @brief Snapshot current resource status and telemetry counters. */
esp_err_t present_resources_status(present_resources_t *, present_resource_status_t *);
/** @brief Destroy stopped resources and release adapters/executors within @p timeout_ms. */
esp_err_t present_resources_destroy(present_resources_t *, uint32_t timeout_ms);
/** @brief Stop engine, TE sync, and hardware resources within @p timeout_ms. */
esp_err_t present_resources_stop(present_resources_t *, uint32_t timeout_ms);
/** @brief Drain work and update executor/TE orientation while retaining physical driver buffers. */
esp_err_t present_resources_set_rotation(present_resources_t *, esp_display_present_engine_rotation_t, uint32_t);
