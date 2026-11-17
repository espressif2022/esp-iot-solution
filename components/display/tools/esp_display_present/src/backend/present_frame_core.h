/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "backend/present_backend_types.h"
#include "backend/present_records.h"

/**
 * @brief Backend frame-record authority (all accesses must hold the owning backend lock).
 */
typedef enum {
    PRESENT_CORE_RUNNING,   /**< Accepting frames. */
    PRESENT_CORE_CLOSING,   /**< Draining pending frames before stop. */
    PRESENT_CORE_QUIESCENT, /**< Quiescent and ready for destruction. */
} present_core_life_t;

/** @brief Per-frame core state and reference counters. */
typedef struct {
    present_record_t common; /**< Shared receipt/result base record. */
    size_t pending;          /**< Accepted/reserved in-flight accesses. */
    size_t leased;           /**< Active producer draw leases. */
    bool sealed;             /**< True once end() sealed the frame. */
    bool canceled;           /**< True once cancel was requested. */
} present_core_frame_t;

/** @brief Frame record pool, cancel history, fault latch, and lifecycle state. */
typedef struct {
    present_core_frame_t *frames; /**< Frame record array. */
    size_t count;                 /**< Record capacity. */
    size_t active;                /**< Active frame count. */
    present_cancel_history_t canceled; /**< Single-entry last-canceled frame token. */
    esp_display_present_backend_fault_t fault; /**< Sticky first fault. */
    present_core_life_t life;     /**< Backend lifecycle state. */
} present_frame_core_t;

/** @brief Allocate and initialize frame record storage for @p count frames. */
esp_err_t present_frame_core_init(present_frame_core_t *core, size_t count);
/** @brief Free frame record storage. */
void present_frame_core_deinit(present_frame_core_t *core);
/** @brief Find an active frame record matching @p token. */
present_core_frame_t *present_frame_core_find(present_frame_core_t *core, uint64_t token);
/** @brief Find a sealed receipt record matching @p receipt. */
present_core_frame_t *present_frame_core_find_receipt(present_frame_core_t *core, esp_display_present_receipt_t receipt);
/** @brief Validate damage hints and admit a new frame under @p lock. */
esp_err_t present_frame_core_admit(present_frame_core_t *core, present_lock_t *lock,
                                   uint32_t width, uint32_t height,
                                   const esp_display_present_frame_request_t *request, esp_display_present_frame_t *out);
/** @brief Allocate a frame record and return its frame token (caller holds lock). */
esp_err_t present_frame_core_begin(present_frame_core_t *core, esp_display_present_frame_t *out);
/** @brief Seal a building frame and expose its external receipt token. */
esp_err_t present_frame_core_seal(present_frame_core_t *core, present_core_frame_t *frame, esp_display_present_receipt_t *out);
/** @brief Reclaim a terminal, unreferenced frame record. */
void present_frame_core_reap(present_frame_core_t *core, present_core_frame_t *frame);
/** @brief Mark a frame's transfers/reads complete and reap if no longer externally referenced. */
void present_frame_core_complete(present_frame_core_t *core, present_core_frame_t *frame);
/** @brief Latch a failure on @p frame and core fault record. */
void present_frame_core_fail(present_frame_core_t *core, present_core_frame_t *frame, uint64_t cookie, esp_err_t error);
/** @brief Release external ownership of a terminal receipt record. */
esp_err_t present_frame_core_release(present_core_frame_t *frame);
/** @brief Query receipt status under @p lock (handles canceled history too). */
esp_err_t present_frame_core_query(present_frame_core_t *core, present_lock_t *lock, esp_display_present_receipt_t receipt,
                                   bool supports_present, esp_display_present_frame_status_t *out);
/** @brief Snapshot sticky backend fault under @p lock. */
esp_err_t present_frame_core_get_fault(present_frame_core_t *core, present_lock_t *lock, esp_display_present_backend_fault_t *out);
/** @brief Poll @p query until @p point is reached or @p timeout expires. */
esp_err_t present_frame_core_wait(present_backend_t *backend,
                                  esp_err_t (*query)(present_backend_t *, esp_display_present_receipt_t, esp_display_present_frame_status_t *),
                                  esp_display_present_receipt_t receipt, esp_display_present_completion_point_t point,
                                  bool supports_present, uint32_t timeout, esp_display_present_frame_status_t *out);
/** @brief Release external receipt ownership under @p lock. */
esp_err_t present_frame_core_release_receipt(present_frame_core_t *core, present_lock_t *lock, esp_display_present_receipt_t receipt);
/** @brief Check whether core is quiescent with no remaining frames. */
esp_err_t present_frame_core_can_destroy(const present_frame_core_t *core);
/** @brief Transition core to QUIESCENT after output/copy quiescence with no open leases/accesses. */
esp_err_t present_frame_core_quiescent(present_frame_core_t *core);
/** @brief Mark @p frame terminal and reap if external receipt was already released. */
void present_frame_core_terminal(present_frame_core_t *, present_core_frame_t *);
