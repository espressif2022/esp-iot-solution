/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef PV2_HOST
#include <pthread.h>

typedef pthread_mutex_t pv2_lock_t;
typedef pthread_t pv2_task_t;
#define PV2_ISR_ATTR
#else
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

typedef portMUX_TYPE pv2_lock_t;
typedef TaskHandle_t pv2_task_t;
#define PV2_ISR_ATTR IRAM_ATTR
#endif

bool pv2_lock_init(pv2_lock_t *lock);
void pv2_lock_destroy(pv2_lock_t *lock);
/* Definitions carry IRAM_ATTR; repeating it on declarations creates different
 * section names with ESP-IDF's __COUNTER__-based attribute. */
void pv2_lock_enter(pv2_lock_t *lock, bool isr);
void pv2_lock_exit(pv2_lock_t *lock, bool isr);
void *pv2_alloc(size_t size);
void *pv2_alloc_pixels(size_t size);
void pv2_free(void *memory);
uint64_t pv2_now_us(void);
uint64_t pv2_now_ms(void);
void pv2_pause(void);
bool pv2_expired(uint64_t start, uint32_t timeout_ms);
/* Process-wide tokens never wrap/reuse; exhaustion returns zero. */
uint64_t pv2_new_token(void);
pv2_task_t pv2_current_task(void);
bool pv2_same_task(pv2_task_t a, pv2_task_t b);

/* Latched binary events. Notification domains use separate waiter events.
 * Signal-before-wait is retained;
 * coalescing is intentional: callers always recheck authoritative state.
 * Destroy requires all signalers/waiters to have exited. */
typedef struct pv2_event pv2_event_t;
pv2_event_t *pv2_event_create(void);
bool pv2_event_signal(pv2_event_t *event, bool from_isr);
bool pv2_event_wait(pv2_event_t *event, uint32_t timeout_ms);
void pv2_event_destroy(pv2_event_t *event);
/* Task-only nonrecursive serialization, never held while waiting for hardware. */
typedef struct pv2_mutex pv2_mutex_t;
pv2_mutex_t *pv2_mutex_create(void);
bool pv2_mutex_take(pv2_mutex_t *mutex, uint32_t timeout_ms);
void pv2_mutex_give(pv2_mutex_t *mutex);
void pv2_mutex_destroy(pv2_mutex_t *mutex);
typedef struct pv2_worker pv2_worker_t;
typedef void (*pv2_worker_entry_fn)(void *user);
pv2_worker_t *pv2_worker_create(pv2_worker_entry_fn entry, void *user, uint32_t stack_bytes, unsigned priority);
/* Entry must be requested to exit by caller before join. Timeout preserves
 * object/thread. Success joins its final exit and allows destroy. */
bool pv2_worker_join(pv2_worker_t *worker, uint32_t timeout_ms);
void pv2_worker_destroy(pv2_worker_t *worker);
