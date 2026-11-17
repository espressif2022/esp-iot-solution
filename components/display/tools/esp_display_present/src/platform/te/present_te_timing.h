/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * @file
 * @brief Pure arithmetic TE window evaluation (no GPIO, OS timer, allocation, or worker lifecycle).
 */
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Linear full-frame TE scan/transfer model parameters (pure arithmetic, no GPIO/timer).
 */
typedef struct {
    uint64_t period_us;       /**< Full TE period in microseconds (0 disables model). */
    uint64_t scan_us;         /**< Active panel scan duration in microseconds. */
    uint64_t edge_to_scan_us; /**< Delay from selected TE edge to scan start in microseconds. */
    uint64_t guard_us;        /**< Safety margin in microseconds. */
    uint64_t jitter_us;       /**< Expected edge/dispatch jitter in microseconds. */
} esp_display_present_te_timing_t;

/** @brief TE transfer window admission outcome. */
typedef enum {
    ESP_DISPLAY_PRESENT_TE_ALLOW,       /**< Transfer fits safely within the current TE window. */
    ESP_DISPLAY_PRESENT_TE_WAIT,        /**< Wait for window opening or next TE edge. */
    ESP_DISPLAY_PRESENT_TE_UNPROVEN,    /**< Insufficient measured evidence to admit transfer safely. */
    ESP_DISPLAY_PRESENT_TE_UNSUPPORTED, /**< Modeled transfer cannot fit in the TE window. */
} esp_display_present_te_admission_t;

/** @brief Evaluated TE transfer admission result and safe phase bounds. */
typedef struct {
    esp_display_present_te_admission_t admission; /**< Admission decision. */
    uint64_t earliest_phase_us;                   /**< Earliest safe start phase from edge in us. */
    uint64_t latest_phase_us;                     /**< Latest safe start phase from edge in us. */
} esp_display_present_te_window_t;

/** @brief Evaluate TE window admission for @p transfer_us at @p now_us relative to @p edge_us. */
esp_display_present_te_window_t esp_display_present_te_window(const esp_display_present_te_timing_t *, uint64_t transfer_us,
                                                              uint64_t edge_us, uint64_t now_us, bool measured);

/**
 * @brief Compute the next opening timestamp inside the current measured edge-to-edge period.
 *
 * Returns false if the caller must wait for a real subsequent TE edge.
 */
bool present_te_next_open(const esp_display_present_te_timing_t *timing,
                          const esp_display_present_te_window_t *window,
                          uint64_t edge_us, uint64_t now_us, uint64_t *next_us);
#ifdef __cplusplus
}
#endif
