/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "pixel/present_pixel_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct present_copy_queue present_copy_queue_t;

/**
 * @brief Single-owner 1-based slot handle (0 is empty/invalid).
 *
 * Invalidated immediately once recycled by present_copy_queue_take() or a
 * completed present_copy_queue_cancel(); never use as a cross-task transaction ID.
 */
typedef struct {
    uint8_t slot; /**< 1-based queue slot index; 0 indicates no slot. */
} present_copy_handle_t;

/**
 * @brief Wake callback invoked outside queue locks when new work is queued in external_worker mode.
 */
typedef void (*present_copy_work_fn)(void *user);

/**
 * @brief Completion callback invoked outside queue locks after a slot enters DONE.
 *
 * @return True if invoked from ISR and a yield is requested; false in task context.
 */
typedef bool (*present_copy_done_fn)(void *user, present_copy_handle_t handle, esp_err_t result, bool from_isr);

/**
 * @brief Copy queue creation configuration.
 */
typedef struct {
    size_t max_jobs;                              /**< Slot capacity (1..PRESENT_MAX_COPY_JOBS). */
    bool external_worker;                         /**< True to run jobs via present_copy_queue_run_one() instead of a dedicated task. */
    present_copy_work_fn on_work;                 /**< Required when external_worker is true; wakes the driving worker. */
    present_copy_done_fn on_done;                 /**< Optional completion callback invoked before retirement completes. */
    esp_display_present_pixel_execute_fn execute; /**< Optional copy executor (NULL uses CPU executor). */
    void *work_user;                              /**< User context passed to on_work. */
    void *done_user;                              /**< User context passed to on_done. */
    void *execute_user;                           /**< User context passed to execute. */
} present_copy_queue_config_t;

/**
 * @brief Create an asynchronous copy queue.
 *
 * @param config Queue configuration; pointer is not retained.
 * @param out Output queue handle.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad config, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t present_copy_queue_create(const present_copy_queue_config_t *config, present_copy_queue_t **out);

/**
 * @brief Validate and enqueue a copy request.
 *
 * @param queue Copy queue instance.
 * @param request Copy descriptor; pixel buffers must stay valid until take() or cancel() returns ESP_OK.
 * @param out_handle Receives the 1-based slot handle on success, or {0} on failure.
 * @return ESP_OK if queued, ESP_ERR_NO_MEM if full, ESP_ERR_INVALID_STATE if closing, or ESP_ERR_INVALID_ARG.
 */
esp_err_t present_copy_queue_submit(present_copy_queue_t *queue, const esp_display_present_copy_request_t *request,
                                    present_copy_handle_t *out_handle);

/**
 * @brief Execute at most one queued job when configured with external_worker=true.
 *
 * @param queue Copy queue instance.
 * @return True if a job was executed or retired; false otherwise.
 */
bool present_copy_queue_run_one(present_copy_queue_t *queue);

/**
 * @brief Poll a finished job and recycle its slot in one lock acquisition.
 *
 * @param queue Copy queue instance.
 * @param handle Handle returned by submit().
 * @param out_result Receives the execution result when ESP_OK is returned.
 * @return ESP_OK if completed and recycled, ESP_ERR_TIMEOUT if still queued/running, or ESP_ERR_INVALID_STATE/ARG.
 */
esp_err_t present_copy_queue_take(present_copy_queue_t *queue, present_copy_handle_t handle, esp_err_t *out_result);

/** @brief Recycle the oldest completed descriptor and return its request. */
esp_err_t present_copy_queue_take_next(present_copy_queue_t *queue,
                                       esp_display_present_copy_request_t *out_request, esp_err_t *out_result);
/** @brief True while a queued/running job reads this rectangle of the same source canvas. */
bool present_copy_queue_source_busy(present_copy_queue_t *queue, const void *pixels,
                                    const esp_display_present_rect_t *area);

/**
 * @brief Request cancellation of a queued job, or recycle it immediately if already finished.
 *
 * @param queue Copy queue instance.
 * @param handle Handle returned by submit().
 * @param out_result Receives the execution result when already DONE and recycled.
 * @return ESP_OK if already DONE and recycled, ESP_ERR_TIMEOUT if still queued/running, or ESP_ERR_INVALID_STATE/ARG.
 */
esp_err_t present_copy_queue_cancel(present_copy_queue_t *queue, present_copy_handle_t handle, esp_err_t *out_result);

/**
 * @brief Stop the queue and wait for in-flight execution, on_done callback, and idle notification to finish.
 *
 * @param queue Copy queue instance.
 * @param timeout_ms Wait budget in ms (0 polls, UINT32_MAX waits indefinitely).
 * @return ESP_OK when stopped, ESP_ERR_TIMEOUT on expiry, ESP_ERR_INVALID_STATE if called from on_done, or ESP_ERR_INVALID_ARG.
 */
esp_err_t present_copy_queue_stop(present_copy_queue_t *queue, uint32_t timeout_ms);

/**
 * @brief Destroy a stopped queue after all slots have been recycled.
 *
 * @param queue Copy queue instance.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not stopped/idle/all-free, or ESP_ERR_INVALID_ARG.
 */
esp_err_t present_copy_queue_destroy(present_copy_queue_t *queue);

#ifdef __cplusplus
}
#endif
