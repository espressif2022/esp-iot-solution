/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef PRESENT_HOST
#include <pthread.h>

typedef pthread_mutex_t present_lock_t;
typedef pthread_t present_task_t;
#define PRESENT_ISR_ATTR
#else
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

typedef portMUX_TYPE present_lock_t;
typedef TaskHandle_t present_task_t;
#define PRESENT_ISR_ATTR IRAM_ATTR
#endif

/** @brief Initialize a short critical-section / spinlock. */
bool present_lock_init(present_lock_t *lock);
/** @brief Destroy a short critical-section / spinlock. */
void present_lock_destroy(present_lock_t *lock);
/** @brief Enter critical-section lock (ISR-aware; definitions carry IRAM_ATTR). */
void present_lock_enter(present_lock_t *lock, bool isr);
/** @brief Exit critical-section lock. */
void present_lock_exit(present_lock_t *lock, bool isr);
/** @brief Request scheduling before returning from an ISR that woke a task. */
void present_yield_from_isr(void);
/** @brief Allocate zero-initialized internal metadata memory. */
void *present_alloc(size_t size);
/** @brief Allocate aligned internal DMA-capable pixel memory. */
void *present_alloc_pixels(size_t size);
/** @brief Allocate aligned pixel memory in external PSRAM when @p external is true, or internal DMA RAM. */
void *present_alloc_pixels_in(size_t size, bool external);
/** @brief Return true if an internal DMA pool block of @p size bytes can be satisfied. */
bool present_internal_dma_pool_available(size_t size);
/** @brief Return true if @p count buffers of @p bytes fit in external PSRAM. */
bool present_psram_can_fit(size_t bytes, size_t count);
/** @brief Free memory allocated by present_alloc*(). */
void present_free(void *memory);
/** @brief Return monotonic timestamp in microseconds. */
uint64_t present_now_us(void);
/** @brief Return monotonic timestamp in milliseconds. */
uint64_t present_now_ms(void);
/** @brief Yield briefly (1 tick / 1 ms) while polling or waiting on micro-windows. */
void present_pause(void);
/** @brief Return true if @p timeout_ms has elapsed since @p start (UINT32_MAX never expires). */
bool present_expired(uint64_t start, uint32_t timeout_ms);
/** @brief Allocate a nonzero, non-wrapping process-wide token (0 on exhaustion). */
uint64_t present_new_token(void);
/** @brief Return the calling task/thread handle. */
present_task_t present_current_task(void);
/** @brief Return true if @p a and @p b refer to the same task/thread. */
bool present_same_task(present_task_t a, present_task_t b);

/**
 * @brief Latched binary notification event (coalescing; destroy after all signalers/waiters exit).
 */
typedef struct present_os_event present_os_event_t;
/** @brief Create a binary notification event. */
present_os_event_t *present_event_create(void);
/** @brief Signal @p event from task or ISR context; returns true if ISR yield is requested. */
bool present_event_signal(present_os_event_t *event, bool from_isr);
/** @brief Wait up to @p timeout_ms for @p event (0 polls, UINT32_MAX waits forever). */
bool present_event_wait(present_os_event_t *event, uint32_t timeout_ms);
/** @brief Destroy a binary notification event after all users have exited. */
void present_event_destroy(present_os_event_t *event);

/** @brief Task-only nonrecursive mutex (never held while waiting on hardware). */
typedef struct present_mutex present_mutex_t;
/** @brief Create a nonrecursive task mutex. */
present_mutex_t *present_mutex_create(void);
/** @brief Acquire @p mutex within @p timeout_ms. */
bool present_mutex_take(present_mutex_t *mutex, uint32_t timeout_ms);
/** @brief Release @p mutex. */
void present_mutex_give(present_mutex_t *mutex);
/** @brief Destroy @p mutex. */
void present_mutex_destroy(present_mutex_t *mutex);

/** @brief Opaque worker task/thread handle. */
typedef struct present_worker present_worker_t;
/** @brief Worker entry callback signature. */
typedef void (*present_worker_entry_fn)(void *user);
/** @brief Create and start a worker task/thread. */
present_worker_t *present_worker_create(const char *name, present_worker_entry_fn entry, void *user, uint32_t stack_bytes, unsigned priority);
/** @brief Read the calling task's scheduling priority (0 on Host). */
unsigned present_task_priority(void);
/** @brief Set the calling task's scheduling priority (no-op on Host). */
void present_task_set_priority(unsigned priority);
/** @brief Set a known-live worker priority from task context (no-op on Host).
 * Caller excludes worker exit/destruction; never call from an ISR. */
void present_worker_set_priority(present_worker_t *worker, unsigned priority);
/** @brief Join an exiting worker within @p timeout_ms; timeout preserves the worker for retry. */
bool present_worker_join(present_worker_t *worker, uint32_t timeout_ms);
/** @brief Free a joined worker handle. */
void present_worker_destroy(present_worker_t *worker);

/** @brief Task-only barrier ensuring any unregistered driver ISR callbacks have exited. */
esp_err_t present_isr_barrier(uint32_t timeout_ms);
