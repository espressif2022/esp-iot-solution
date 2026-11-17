/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @file
 * @brief GPIO TE measurement and admission coordinator (composes timing and timer; never submits LCD pixels).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_display_present_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Default panel-bus data-line count used when unspecified. */
#define ESP_DISPLAY_PRESENT_TE_DATA_LINES_DEFAULT         1

/** @brief Opaque GPIO TE synchronization context. */
typedef struct esp_display_present_te_sync_context esp_display_present_te_sync_context_t;

/** @brief Return true if TE synchronization configuration is valid and enabled. */
bool esp_display_present_te_sync_is_enabled(const esp_display_present_te_sync_config_t *cfg);
/** @brief Return framework-owned TE sync metadata bytes (excluding SDK timer allocation). */
size_t esp_display_present_te_sync_memory(void);

/**
 * @brief Create GPIO TE sync and auto-detect blanking edges from the idle GPIO level.
 *
 * @p prefer_refresh_end selects the initial sync edge preference before the
 * measured schedule takes over.
 */
esp_err_t esp_display_present_te_sync_create(const esp_display_present_te_sync_config_t *cfg,
                                             bool prefer_refresh_end,
                                             esp_display_present_te_sync_context_t **out_ctx);

/** @brief Record transfer submission start timestamp for TE phase tracking. */
void esp_display_present_te_sync_record_tx_start(esp_display_present_te_sync_context_t *ctx);

/** @brief Record successful transfer completion timestamp (ISR-safe). */
void esp_display_present_te_sync_record_tx_done(esp_display_present_te_sync_context_t *ctx);

/** @brief Read latest measured TE period, transfer duration, and blanking duration in microseconds. */
void esp_display_present_te_sync_get_timing(
    const esp_display_present_te_sync_context_t *ctx,
    int64_t *out_period_us,
    int64_t *out_last_tx_us,
    int64_t *out_blanking_us);

/** @brief Wakeup callback invoked from GPIO ISR or phase timer when a TE window opens. */
typedef bool (*esp_display_present_te_wakeup_fn)(void *, bool from_isr);
/** @brief Register optional wakeup callback invoked on TE edges or phase openings. */
void esp_display_present_te_sync_set_wakeup(esp_display_present_te_sync_context_t *, esp_display_present_te_wakeup_fn, void *);
/** @brief Tear down TE sync and drain GPIO/timer callbacks within @p timeout_ms. */
esp_err_t esp_display_present_te_sync_destroy_budget(esp_display_present_te_sync_context_t *, uint32_t timeout_ms);
/** @brief Tear down TE sync with default drain budget. */
esp_err_t esp_display_present_te_sync_destroy_checked(esp_display_present_te_sync_context_t *);
/**
 * @brief Nonblocking TE admission check for a transfer starting after @p after.
 *
 * Returns ESP_ERR_TIMEOUT when waiting for the next edge/window, or
 * ESP_ERR_NOT_SUPPORTED if the measured window cannot fit the transfer.
 */
esp_err_t esp_display_present_te_sync_admit(esp_display_present_te_sync_context_t *, size_t, uint64_t, uint64_t *);
/** @brief Abort an in-flight TE transfer timing sample after rejection/failure (ISR-safe). */
void esp_display_present_te_sync_abort_tx(esp_display_present_te_sync_context_t *);

#ifdef __cplusplus
}
#endif
