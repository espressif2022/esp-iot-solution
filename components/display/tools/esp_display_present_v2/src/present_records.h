/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "present_backend.h"
#include "present_os.h"
#include "present_limits.h"

/* Private value/record policy only. Caller owns locking, completion timing,
 * pool storage and recycling. No hardware, callbacks or buffer ownership here.
 * Embed first in backend-specific records when using pv2_record_find. */
typedef struct {
    uint64_t token;
    bool external;
    bool terminal;
    esp_err_t result;
} pv2_record_t;

static inline bool pv2_valid_rect(uint32_t width, uint32_t height, const pv2_rect_t *a)
{
    return a && a->width && a->height && a->x < width && a->y < height &&
           a->width <= width - a->x && a->height <= height - a->y;
}
static inline esp_err_t pv2_validate_damage(uint32_t width, uint32_t height,
                                            const pv2_rect_t *areas, size_t count)
{
    if (count > PV2_MAX_DAMAGE_AREAS || (count && !areas)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!pv2_valid_rect(width, height, &areas[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}
static inline uint32_t pv2_remaining_ms(uint64_t start, uint32_t timeout)
{
    uint64_t elapsed = pv2_now_ms() - start;
    return timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : (uint32_t)(timeout - elapsed);
}
static inline void *pv2_record_find(void *records, size_t count, size_t stride,
                                    uint64_t token, bool external)
{
    if (!token) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        pv2_record_t *record = (pv2_record_t *)((uint8_t *)records + i * stride);
        if (record->token == token && (!external || record->external)) {
            return record;
        }
    }
    return NULL;
}
static inline void PV2_ISR_ATTR pv2_record_fail(pv2_record_t *record, esp_err_t error)
{
    if (record && record->result == ESP_OK && error != ESP_OK) {
        record->result = error;
    }
}
static inline void PV2_ISR_ATTR pv2_fault_first(pv2_backend_fault_t *fault,
                                                uint64_t token, uint64_t cookie, esp_err_t error)
{
    if (error != ESP_OK && fault->result == ESP_OK) {
        fault->result = error;
        fault->frame.value = token;
        fault->transaction_cookie = cookie;
    }
}
/* Evidence is supplied by the backend: terminal != successful presentation. */
static inline pv2_frame_status_t pv2_record_status(const pv2_record_t *record,
                                                   bool sealed, bool released, bool terminal, bool presented)
{
    return (pv2_frame_status_t) {
        .sealed = sealed, .source_released = released, .terminal = terminal,
        .present_confirmed = presented, .result = record->result
    };
}

/* One-entry idempotence history, not an unbounded cancellation registry.
 * The backend publishes only after all accesses for that frame have ended. */
typedef struct {
    uint64_t token;
} pv2_cancel_history_t;
static inline bool pv2_cancel_matches(const pv2_cancel_history_t *history, uint64_t token)
{
    return token && history->token == token;
}
static inline void PV2_ISR_ATTR pv2_cancel_publish(pv2_cancel_history_t *history, uint64_t token)
{
    history->token = token;
}
