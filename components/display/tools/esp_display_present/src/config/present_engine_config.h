/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_config.h"
#include "backend/present_output_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Target backend implementation kind.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_REGION = 0, /**< Partial-region panel-IO backend. */
    ESP_DISPLAY_PRESENT_ENGINE_TE,         /**< Tearing-synchronized full-frame compose backend. */
    ESP_DISPLAY_PRESENT_ENGINE_FB,         /**< RGB/MIPI-DSI framebuffer scanout backend. */
} esp_display_present_engine_backend_t;

/**
 * @brief Engine preset mode mapped from public target mode configuration.
 */
typedef enum {
    ESP_DISPLAY_PRESENT_MODE_DEFAULT = 0,          /**< Use backend_kind and backend config verbatim. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE,          /**< Panel-IO direct transfer without TE avoidance. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_FULL,   /**< Double-buffered full-frame scanout. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_FULL,   /**< Triple-buffered full-frame scanout. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_DIRECT, /**< Double-buffered direct dirty-area scanout. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TRIPLE_PARTIAL,/**< Triple-buffered partitioned scanout. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_TE_SYNC,       /**< TE-synchronized panel-IO transfer. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_DOUBLE_PARTIAL,/**< Double-buffered partitioned scanout. */
    ESP_DISPLAY_PRESENT_ENGINE_MODE_AUTO,          /**< Resolve FB 2/3 buffers to PARTIAL and IO by TE budgets. */
} esp_display_present_present_mode_t;

/**
 * @brief Derived engine scheduling profile (describes policy, not a strict thread count).
 */
typedef enum {
    ESP_DISPLAY_PRESENT_EXEC_COOPERATIVE = 0, /**< No backend worker task; async_copy may still create a pixel worker. */
    ESP_DISPLAY_PRESENT_EXEC_REGION_DEFERRED, /**< REGION with backend worker; READY tiles submit only from backend worker. */
    ESP_DISPLAY_PRESENT_EXEC_TE_SHARED,       /**< TE with copy_on_backend_worker; backend worker drains copy queue and submits. */
    ESP_DISPLAY_PRESENT_EXEC_TE_SPLIT,        /**< TE with dedicated copy queue worker and backend-only submission. */
    ESP_DISPLAY_PRESENT_EXEC_FB_SHARED,       /**< FB with copy_on_backend_worker; backend worker drains copy queue and advances scanout. */
    ESP_DISPLAY_PRESENT_EXEC_FB_SPLIT,        /**< FB without copy_on_backend_worker (sync or dedicated copy worker depending on config). */
} esp_display_present_exec_mode_t;

/**
 * @brief Engine creation configuration.
 */
typedef struct esp_display_present_engine_config {
    esp_display_present_backend_config_t backend;     /**< Backend geometry, buffers, and panel adapter. */
    uint32_t timeout_ms;                              /**< Default wait budget in ms (0 polls, UINT32_MAX waits indefinitely). */
    esp_display_present_engine_backend_t backend_kind;/**< Target backend kind (0 selects REGION). */
    esp_display_present_present_mode_t mode;          /**< Named preset mode; FB requires >= 2 buffers and rejects NONE/active_writes. */
    bool background_service;                          /**< Enable backend worker task; required by deferred_submit and copy_on_backend_worker. */
    uint32_t worker_stack_bytes;                      /**< Backend worker stack size in bytes (0 uses 4096; ignored on Host). */
    unsigned worker_priority;                         /**< Backend worker priority (0 uses 5; ignored on Host). */
    esp_display_present_output_caps_t output_caps;    /**< Output alignment and memory constraints (0 alignment means 1). */
    uint32_t transfer_timeout_ms;                     /**< Per panel.submit completion deadline in ms (0 disables); timeout latches a sticky fault. */
} esp_display_present_engine_config_t;

/**
 * @brief Derive the scheduling profile from an engine configuration.
 *
 * @param config Resolved or caller engine configuration.
 * @return Derived scheduling profile.
 */
static inline esp_display_present_exec_mode_t esp_display_present_resolve_exec_mode(const esp_display_present_engine_config_t *config)
{
    if (!config || !config->background_service) {
        return ESP_DISPLAY_PRESENT_EXEC_COOPERATIVE;
    }
    switch (config->backend_kind) {
    case ESP_DISPLAY_PRESENT_ENGINE_REGION:
        return ESP_DISPLAY_PRESENT_EXEC_REGION_DEFERRED;
    case ESP_DISPLAY_PRESENT_ENGINE_TE:
        return config->backend.copy_on_backend_worker ? ESP_DISPLAY_PRESENT_EXEC_TE_SHARED : ESP_DISPLAY_PRESENT_EXEC_TE_SPLIT;
    case ESP_DISPLAY_PRESENT_ENGINE_FB:
        return config->backend.copy_on_backend_worker ? ESP_DISPLAY_PRESENT_EXEC_FB_SHARED : ESP_DISPLAY_PRESENT_EXEC_FB_SPLIT;
    default:
        return ESP_DISPLAY_PRESENT_EXEC_COOPERATIVE;
    }
}

#ifdef __cplusplus
}
#endif
