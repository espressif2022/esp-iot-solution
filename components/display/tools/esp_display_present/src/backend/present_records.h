/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "platform/present_os.h"
#include "platform/present_limits.h"

/**
 * @brief Base record header embedded first in backend-specific frame records.
 */
typedef struct {
    uint64_t token;   /**< Instance-scoped frame token (0 is empty). */
    bool external;    /**< True while receipt remains externally queryable/owned. */
    bool terminal;    /**< True once frame work has reached a terminal state. */
    esp_err_t result; /**< First latched error or ESP_OK. */
} present_record_t;

/** @brief Return true if @p area is non-empty and inside [0, width) x [0, height). */
static inline bool present_valid_rect(uint32_t width, uint32_t height, const esp_display_present_rect_t *area)
{
    return area && area->width && area->height && area->x < width && area->y < height &&
           area->width <= width - area->x && area->height <= height - area->y;
}
/** @brief Validate an array of @p count damage rectangles against @p width x @p height. */
static inline esp_err_t present_validate_damage(uint32_t width, uint32_t height,
                                                const esp_display_present_rect_t *areas, size_t count)
{
    if (count > PRESENT_MAX_DAMAGE_AREAS || (count && !areas)) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!present_valid_rect(width, height, &areas[i])) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}
/** @brief Compute remaining milliseconds from @p start until @p timeout (UINT32_MAX waits forever). */
static inline uint32_t present_remaining_ms(uint64_t start, uint32_t timeout)
{
    uint64_t elapsed = present_now_ms() - start;
    return timeout == UINT32_MAX ? UINT32_MAX : elapsed >= timeout ? 0 : (uint32_t)(timeout - elapsed);
}
/** @brief Find a record with matching @p token (and @c external flag when @p external is true). */
static inline void *present_record_find(void *records, size_t count, size_t stride,
                                        uint64_t token, bool external)
{
    if (!token) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        present_record_t *record = (present_record_t *)((uint8_t *)records + i * stride);
        if (record->token == token && (!external || record->external)) {
            return record;
        }
    }
    return NULL;
}
/** @brief Record the first non-OK error on @p record without overwriting an earlier failure. */
static inline void PRESENT_ISR_ATTR present_record_fail(present_record_t *record, esp_err_t error)
{
    if (record && record->result == ESP_OK && error != ESP_OK) {
        record->result = error;
    }
}
/** @brief Latch the first non-OK fault and context into @p fault. */
static inline void PRESENT_ISR_ATTR present_fault_first(esp_display_present_backend_fault_t *fault,
                                                        uint64_t token, uint64_t cookie, esp_err_t error)
{
    if (error != ESP_OK && fault->result == ESP_OK) {
        fault->result = error;
        fault->frame.value = token;
        fault->transaction_cookie = cookie;
    }
}
/** @brief Build a public frame status snapshot from @p record and explicit evidence flags. */
static inline esp_display_present_frame_status_t present_record_status(const present_record_t *record,
                                                                       bool sealed, bool released, bool terminal, bool presented)
{
    return (esp_display_present_frame_status_t) {
        .sealed = sealed,
        .src_released = released,
        .terminal = terminal,
        .present_confirmed = presented,
        .result = record->result
    };
}

/** @brief Single-entry idempotent cancellation history published only after all frame accesses end. */
typedef struct {
    uint64_t token; /**< Last canceled frame token. */
} present_cancel_history_t;
/** @brief Return true if @p token matches the last published canceled frame. */
static inline bool present_cancel_matches(const present_cancel_history_t *history, uint64_t token)
{
    return token && history->token == token;
}
/** @brief Publish @p token to the single-entry cancellation history. */
static inline void PRESENT_ISR_ATTR present_cancel_publish(present_cancel_history_t *history, uint64_t token)
{
    history->token = token;
}
