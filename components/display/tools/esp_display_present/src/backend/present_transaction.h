/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "platform/present_os.h"
#include "backend/present_backend_types.h"
/**
 * @brief Per-submission acceptance and early-completion state under the owning backend lock.
 */
typedef struct {
    bool done;        /**< True once a completion callback has arrived. */
    esp_err_t result; /**< Completion result latched from the first error. */
} present_transaction_t;

/** @brief Resolved outcome combining driver acceptance with any early completion callback. */
typedef struct {
    bool rejected;    /**< True only when REJECTED and no completion callback arrived. */
    bool complete;    /**< True when completion callback already ran during submit(). */
    esp_err_t result; /**< Authoritative error code. */
} present_transaction_resolution_t;

/** @brief Reset transaction state before invoking panel.submit(). */
static inline void present_transaction_prepare(present_transaction_t *transaction)
{
    *transaction = (present_transaction_t) {
        0
    };
}
/** @brief Latch transaction completion from ISR or task callback; returns true on first completion. */
static inline bool PRESENT_ISR_ATTR present_transaction_done(present_transaction_t *transaction, esp_err_t error)
{
    if (transaction->done) {
        return false;
    }
    transaction->done = true;
    if (transaction->result == ESP_OK) {
        transaction->result = error;
    }
    return true;
}
/** @brief Combine panel.submit() acceptance with early callback state. */
static inline present_transaction_resolution_t present_transaction_resolve(
    const present_transaction_t *transaction, esp_display_present_tx_result_t result)
{
    bool rejected = result.acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED && !transaction->done;
    esp_err_t error = transaction->result != ESP_OK ? transaction->result : result.result;
    if (result.acceptance != ESP_DISPLAY_PRESENT_TX_ACCEPTED && error == ESP_OK) {
        error = ESP_FAIL;
    }
    return (present_transaction_resolution_t) {
        rejected, transaction->done, error
    };
}
