/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_os.h"
#include "present_backend.h"
/* Acceptance and completion are independent facts. Caller holds its short
 * lock; no callbacks, allocation or driver calls here. Rejection is retryable
 * only when no completion has proved access. Fault alone never proves drain. */
typedef struct {
    bool done;
    esp_err_t result;
} pv2_transaction_t;
typedef struct {
    bool rejected, complete;
    esp_err_t result;
} pv2_transaction_resolution_t;
static inline void pv2_transaction_prepare(pv2_transaction_t *t)
{
    *t = (pv2_transaction_t) {
        0
    };
}
static inline bool PV2_ISR_ATTR pv2_transaction_done(pv2_transaction_t *t, esp_err_t error)
{
    if (t->done) {
        return false;
    }
    t->done = true;
    if (t->result == ESP_OK) {
        t->result = error;
    }
    return true;
}
static inline pv2_transaction_resolution_t pv2_transaction_resolve(
    const pv2_transaction_t *t, pv2_tx_result_t result)
{
    bool rejected = result.acceptance == PV2_TX_REJECTED && !t->done;
    esp_err_t error = t->result != ESP_OK ? t->result : result.result;
    if (result.acceptance != PV2_TX_ACCEPTED && error == ESP_OK) {
        error = ESP_FAIL;
    }
    return (pv2_transaction_resolution_t) {
        rejected, t->done, error
    };
}
