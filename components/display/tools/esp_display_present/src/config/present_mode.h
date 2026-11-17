/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_display_present_config.h"
#include "config/present_engine_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Select default pool/mode policy, not a ceiling on supported capabilities. */
typedef enum {
    ESP_DISPLAY_PRESENT_PLAN_CURRENT_DEFAULTS, /**< Current defaults (2 compose buffers for TE when unspecified). */
    ESP_DISPLAY_PRESENT_PLAN_LEGACY_DEFAULTS,  /**< Legacy compatibility defaults (1 compose buffer for TE). */
} esp_display_present_plan_defaults_t;

/**
 * @brief Resolved mode policy without heap probing, hardware calls, or allocation.
 *
 * RGB/DSI scanout requires at least two FBs and rejects NONE.
 * @c frame_buffers counts driver FBs, not TE composition images or draw tiles.
 */
typedef struct {
    esp_display_present_engine_backend_t kind;     /**< Selected backend kind. */
    esp_display_present_present_mode_t engine_mode; /**< Normalized engine mode. */
    esp_display_present_render_contract_t contract; /**< Active draw contract. */
    uint8_t frame_buffers;                          /**< Driver FB count (0 for non-FB backends). */
    bool active_writes;                             /**< Allow writing an active scanout buffer (single-FB only). */
} present_mode_resolution_t;

/** @brief Map an internal engine preset mode to the public presentation mode enum. */
esp_display_present_mode_t present_mode_to_public(esp_display_present_present_mode_t);
/** @brief Resolve public target config into backend kind, contract, and FB count policy. */
esp_err_t present_mode_resolve_target(const esp_display_present_target_config_t *, present_mode_resolution_t *);
/** @brief Normalize an engine config according to defaults and TE state. */
esp_err_t present_mode_resolve_engine(const esp_display_present_engine_config_t *, esp_display_present_plan_defaults_t,
                                      bool te_enabled, esp_display_present_engine_config_t *);

#ifdef __cplusplus
}
#endif
