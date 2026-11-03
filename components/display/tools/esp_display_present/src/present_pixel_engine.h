/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/** Experimental API: synchronous CPU or queued executor with bounded tickets. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_display_present_pixel_engine esp_display_present_pixel_engine_t;

/** Opaque, instance-scoped generation token. Zero is invalid. */
typedef struct {
    uint64_t value;
} esp_display_present_copy_ticket_t;

typedef enum {
    ESP_DISPLAY_PRESENT_FORMAT_RGB565,
    ESP_DISPLAY_PRESENT_FORMAT_RGB888,
} esp_display_present_engine_pixel_format_t;

/** Byte order of a multi-byte pixel scalar, not RGB/BGR channel order.
 * RGB888 uses R,G,B byte sequence; only NATIVE is valid for RGB888. */
typedef enum {
    ESP_DISPLAY_PRESENT_ORDER_NATIVE,
    ESP_DISPLAY_PRESENT_ORDER_SWAPPED,
} esp_display_present_byte_order_t;

typedef enum {
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0,
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_90,
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_180,
    ESP_DISPLAY_PRESENT_ENGINE_ROTATE_270,
} esp_display_present_engine_rotation_t;

/** x/y are inclusive origins, width/height are extents; no inclusive ends. */
typedef struct {
    uint32_t x, y, width, height;
} esp_display_present_rect_t;

typedef struct {
    uint32_t width, height;
    size_t stride_bytes;
    esp_display_present_engine_pixel_format_t format;
    esp_display_present_byte_order_t byte_order;
} esp_display_present_layout_t;

typedef struct {
    const void *pixels;
    size_t capacity_bytes;
    esp_display_present_layout_t layout;
} esp_display_present_source_t;

typedef struct {
    void *pixels;
    size_t capacity_bytes;
    esp_display_present_layout_t layout;
} esp_display_present_surface_t;

typedef struct {
    esp_display_present_source_t source;
    esp_display_present_surface_t destination;
    esp_display_present_rect_t source_area;
    uint32_t destination_x, destination_y;
    esp_display_present_engine_rotation_t rotation; /* Clockwise around source_area, then translate. */
} esp_display_present_copy_request_t;

typedef enum {
    ESP_DISPLAY_PRESENT_COPY_QUEUED,
    ESP_DISPLAY_PRESENT_COPY_RUNNING,
    ESP_DISPLAY_PRESENT_COPY_SUCCEEDED,
    ESP_DISPLAY_PRESENT_COPY_FAILED,
} esp_display_present_copy_state_t;

typedef struct {
    esp_display_present_copy_state_t state;
    esp_err_t result; /* Operation result; meaningful only in terminal states. */
    bool fault_pending; /* Hardware failure detected, access may still continue. */
} esp_display_present_copy_status_t;

/** Invoked outside engine spinlock when an accepted ticket reaches a terminal
 * state (SUCCEEDED/FAILED) or records a non-terminal fault_pending. Returning
 * true from ISR context requests a context switch. Current CPU notifications
 * run on the submitter in synchronous mode, or the worker in queued mode. The status pointer is valid only
 * during the callback. Callback may query/release the terminal ticket; in that
 * case submit returns an already released token. Pick exactly one record owner.
 * Reentrant submit returns backpressure. Blocking wait on unfinished work and
 * destruction are prohibited. stop from this callback returns INVALID_STATE
 * without changing lifecycle; stop from another task waits for callback exit.
 * Callback user context must stay valid until successful stop and caller join. */
typedef bool (*esp_display_present_pixel_event_fn)(void *user, esp_display_present_copy_ticket_t ticket, const esp_display_present_copy_status_t *status, bool from_isr);

/** Optional blocking executor, called only by the engine worker. On return
 * ALL reads/writes must have ended, even on failure. Hardware implementations
 * must drain/abort safely before returning; an uncertain DMA error cannot be
 * returned as terminal failure while hardware still owns memory. Borrowed
 * context stays valid until successful stop. NULL executes the CPU reference. */
typedef esp_err_t (*esp_display_present_pixel_execute_fn)(void *user, const esp_display_present_copy_request_t *request);

typedef struct {
    size_t max_jobs; /* Bounded completion records, 1..64. */
    esp_display_present_pixel_event_fn on_event; /* Optional completion/fault notification. */
    void *user;
    bool asynchronous; /* FIFO worker; false preserves synchronous CPU behavior. */
    esp_display_present_pixel_execute_fn execute; /* Requires asynchronous == true. */
    void *execute_user;
} esp_display_present_pixel_config_t;

/**
 * @brief Logical-to-physical clockwise geometry and its inverse for touch points.
 * width/height are logical dimensions; quarter turns swap physical dimensions.
 *
 * @param width Logical width in pixels.
 * @param height Logical height in pixels.
 * @param rotation Clockwise logical-to-physical orientation.
 * @param area Input rectangle; see the coordinate and ownership contract above.
 * @param out_area Output area.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_rotate_rect(uint32_t width, uint32_t height, esp_display_present_engine_rotation_t rotation, const esp_display_present_rect_t *area, esp_display_present_rect_t *out_area);

/**
 * @brief Unrotate point.
 *
 * @param width Logical width in pixels.
 * @param height Logical height in pixels.
 * @param rotation Clockwise logical-to-physical orientation.
 * @param physical_x Physical panel X coordinate in pixels.
 * @param physical_y Physical panel Y coordinate in pixels.
 * @param logical_x Receives the logical X coordinate.
 * @param logical_y Receives the logical Y coordinate.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_unrotate_point(uint32_t width, uint32_t height, esp_display_present_engine_rotation_t rotation, uint32_t physical_x, uint32_t physical_y, uint32_t *logical_x, uint32_t *logical_y);

/**
 * @brief Stateless validated CPU oracle/fallback for platform executors. Caller owns
 * both borrows until return; same validation and overlap rules as submit.
 *
 * @param request Request descriptor, subject to the ownership contract above.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_validate_request(const esp_display_present_copy_request_t *request);

/**
 * @brief Pixel execute cpu.
 *
 * @param request Request descriptor, subject to the ownership contract above.
 * @return ESP_OK on success, or an error as specified by the lifecycle contract above.
 */
esp_err_t esp_display_present_pixel_execute_cpu(const esp_display_present_copy_request_t *request);

/**
 * @brief Default is synchronous CPU execution. asynchronous=true queues descriptors
 * to one worker, serializing overlapping writes in submission order.
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

/**
 * @brief ESP_OK consumes an access borrow, not ownership of the allocations.
 * Copy descriptors by value before returning; pixel allocations remain valid
 * and untouched by the caller until terminal completion.
 *
 * Overlap contract:
 * - Different buffers must not overlap in address range.
 * - In-place conversion (source.pixels == destination.pixels) is permitted ONLY
 * when rotation == ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, source_area origin == (destination_x,
 * destination_y), and source.layout (width, height, stride, format) matches
 * destination.layout.
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
