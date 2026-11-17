/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque asynchronous/synchronous pixel engine handle. */
typedef struct esp_display_present_pixel_engine esp_display_present_pixel_engine_t;

/** @brief Opaque instance-scoped copy generation token (0 is invalid). */
typedef struct {
    uint64_t value; /**< Token value. */
} esp_display_present_copy_ticket_t;

/** @brief Copy job lifecycle state. */
typedef enum {
    ESP_DISPLAY_PRESENT_COPY_QUEUED,    /**< Waiting in queue for worker execution. */
    ESP_DISPLAY_PRESENT_COPY_RUNNING,   /**< Currently executing on worker/submitter. */
    ESP_DISPLAY_PRESENT_COPY_SUCCEEDED, /**< Completed with ESP_OK. */
    ESP_DISPLAY_PRESENT_COPY_FAILED,    /**< Completed with a non-OK error or canceled. */
} esp_display_present_copy_state_t;

/** @brief Observed copy job state and completion result. */
typedef struct {
    esp_display_present_copy_state_t state; /**< Current job state. */
    esp_err_t result;                       /**< Operation result (valid in terminal states). */
    bool fault_pending;                     /**< Hardware failure detected while access may continue. */
} esp_display_present_copy_status_t;

/**
 * @brief Completion/fault callback invoked outside engine spinlocks.
 *
 * Callback may query/release the terminal ticket; stop() from inside this
 * callback returns ESP_ERR_INVALID_STATE.
 */
typedef bool (*esp_display_present_pixel_event_fn)(void *user, esp_display_present_copy_ticket_t ticket, const esp_display_present_copy_status_t *status, bool from_isr);

/** @brief Pixel engine creation configuration. */
typedef struct {
    size_t max_jobs;                              /**< Bounded completion records (1..64). */
    esp_display_present_pixel_event_fn on_event;  /**< Optional completion/fault notification. */
    void *user;                                   /**< User context passed to @c on_event / @c on_work. */
    bool asynchronous;                            /**< True for FIFO queued execution; false for synchronous CPU execution. */
    bool external_worker;                         /**< True for queue without dedicated task (requires asynchronous and on_work). */
    void (*on_work)(void *user);                  /**< Wake external consumer (must not execute or block). */
    esp_display_present_pixel_execute_fn execute; /**< Optional blocking executor on submitter or worker. */
    void *execute_user;                           /**< User context passed to @c execute. */
} esp_display_present_pixel_config_t;

/**
 * @brief Default is synchronous CPU execution. asynchronous=true queues descriptors
 * to one worker, serializing overlapping writes in submission order. On IDF
 * the worker adopts the first successful submitting task's priority; later
 * submissions do not change it. Host execution does not set thread priority.
 *
 * Contention vs invalid state:
 * - Engine stopped/closing returns ESP_ERR_INVALID_STATE.
 * - Engine currently executing (or record table full) returns ESP_ERR_NO_MEM
 * so callers can distinguish retryable contention/backpressure from a broken
 * lifecycle state.
 *
 * @param config Configuration; the pointer is not retained.
 * @param out_engine Output engine.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_create(const esp_display_present_pixel_config_t *config, esp_display_present_pixel_engine_t **out_engine);

/** @brief Execute at most one queued job on an external worker outside the presenter gate. */
bool present_pixel_run_one(esp_display_present_pixel_engine_t *engine);

/**
 * @brief ESP_OK consumes an access borrow, not ownership of the allocations.
 * Copy descriptors by value before returning; pixel allocations remain valid
 * and untouched by the caller until terminal completion.
 *
 * Overlap contract:
 * - Different buffers must not overlap in address range.
 * - In-place conversion (src.pixels == dst.pixels) is permitted ONLY
 * when rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, src_area origin == (dst_x,
 * dst_y), and src.layout (width, height, stride, format) matches
 * dst.layout.
 *
 * Non-OK means NOT ACCEPTED: no queued work, no active hardware, ticket=0.
 * Completion may precede return in either mode; still produces a ticket.
 *
 * @param engine Pixel engine instance.
 * @param request Request descriptor, subject to the ownership contract above.
 * @param out_ticket Output ticket.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_submit(esp_display_present_pixel_engine_t *engine, const esp_display_present_copy_request_t *request, esp_display_present_copy_ticket_t *out_ticket);

/**
 * @brief Pixel query.
 *
 * @param engine Pixel engine instance.
 * @param ticket Instance-scoped copy token.
 * @param out_status Output status.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_query(esp_display_present_pixel_engine_t *engine, esp_display_present_copy_ticket_t ticket, esp_display_present_copy_status_t *out_status);

/**
 * @brief Timeout ends only the wait. ESP_OK means terminal state observed; inspect
 * out_status.result for operation success. FAILED guarantees no hardware
 * access remains. timeout_ms=0 polls; UINT32_MAX waits indefinitely.
 *
 * @param engine Pixel engine instance.
 * @param ticket Instance-scoped copy token.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @param out_status Output status.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_wait(esp_display_present_pixel_engine_t *engine, esp_display_present_copy_ticket_t ticket, uint32_t timeout_ms, esp_display_present_copy_status_t *out_status);

/**
 * @brief Cancel queued work before executor entry; running work is drained, never
 * killed. ESP_OK means terminal observed (it may have succeeded if already
 * running). Timeout retains the request/borrow; queued cancellation remains
 * requested and is processed by the worker. Query status for final result.
 * Canceled queued jobs terminate FAILED/ESP_ERR_INVALID_STATE.
 *
 * @param engine Pixel engine instance.
 * @param ticket Instance-scoped copy token.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @param out_status Output status.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_cancel(esp_display_present_pixel_engine_t *engine, esp_display_present_copy_ticket_t ticket, uint32_t timeout_ms, esp_display_present_copy_status_t *out_status);

/**
 * @brief Recycle only a terminal record. Never cancels hardware or frees pixels.
 *
 * @param engine Pixel engine instance.
 * @param ticket Instance-scoped copy token.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_release(esp_display_present_pixel_engine_t *engine, esp_display_present_copy_ticket_t ticket);

/**
 * @brief Reject new submits, drain accepted work AND notification callbacks.
 * Timeout retains all resources. Calling from this engine's callback is rejected.
 * External-worker queues cancel jobs that have not started, without accessing
 * their buffers; a running job still owns its buffers until executor return.
 * Other terminal ticket query/release operations remain available. Lifecycle
 * calls (stop/destroy) must be serialized by the owner; one waiter per ticket.
 *
 * @param engine Pixel engine instance.
 * @param timeout_ms Total wait budget in milliseconds; zero polls and UINT32_MAX waits indefinitely.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_stop(esp_display_present_pixel_engine_t *engine, uint32_t timeout_ms);

/**
 * @brief Requires successful stop, release of every ticket, and external serialization
 * with all callers (including a submit whose completion callback just exited).
 *
 * @param engine Pixel engine instance.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_destroy(esp_display_present_pixel_engine_t *engine);

#ifdef __cplusplus
}
#endif
