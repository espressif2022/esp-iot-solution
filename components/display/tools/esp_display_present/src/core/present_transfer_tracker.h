/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "platform/present_limits.h"
#include "platform/present_os.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timestamped in-flight hardware transaction entry. */
typedef struct {
    uint64_t cookie;     /**< Panel transaction cookie. */
    uint64_t started_us; /**< Submission timestamp in monotonic microseconds. */
    bool reported;       /**< True once expiration has been reported. */
} present_transfer_entry_t;

/** @brief Per-push panel transfer deadline tracker (timing evidence only). */
typedef struct {
    uint64_t timeout_us;            /**< Per-transfer timeout in microseconds (0 disables). */
    present_transfer_entry_t *entries; /**< Bounded entry array allocated on init. */
} present_transfer_tracker_t;

/** @brief Initialize the transfer timeout tracker for @p timeout_ms (0 disables). */
esp_err_t present_transfer_init(present_transfer_tracker_t *, uint32_t timeout_ms);
/** @brief Free tracker storage. */
void present_transfer_deinit(present_transfer_tracker_t *);
/** @brief Return metadata bytes required for @p timeout_ms. */
size_t present_transfer_memory(uint32_t timeout_ms);
/** @brief Record start of a submitted panel transaction under @p control lock. */
size_t present_transfer_begin(present_transfer_tracker_t *, present_lock_t *, uint64_t cookie);
/** @brief Roll back a recorded transaction entry after immediate panel rejection. */
void present_transfer_rejected(present_transfer_tracker_t *, present_lock_t *, size_t index, uint64_t cookie);
/** @brief Clear all tracked entries after backend quiesce. */
void present_transfer_quiesced(present_transfer_tracker_t *, present_lock_t *);
/** @brief Mark completion of @p event under lock; returns true if matched and @p timestamp_us set. */
bool present_transfer_finish(present_transfer_tracker_t *, present_lock_t *,
                             const esp_display_present_backend_event_t *, bool isr, uint64_t *timestamp_us);
/** @brief Collect expired cookies as of @p now (task context, engine gate held). */
size_t present_transfer_expired(present_transfer_tracker_t *, present_lock_t *, uint64_t now, uint64_t *cookies);
/** @brief Return milliseconds until the next transfer deadline (UINT32_MAX if none). */
uint32_t present_transfer_wait_ms(present_transfer_tracker_t *, present_lock_t *);

#ifdef __cplusplus
}
#endif
