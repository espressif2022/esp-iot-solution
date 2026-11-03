/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_os.h"
#include "present_limits.h"
#include <stdlib.h>
#ifdef PRESENT_HOST
#include <time.h>
#else
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#if CONFIG_ESP_IPC_ENABLE
#include "esp_ipc.h"
#endif
#endif

#ifdef PRESENT_HOST
static present_lock_t token_lock = PTHREAD_MUTEX_INITIALIZER;
#else
static present_lock_t token_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static uint64_t next_token = 1;

uint64_t present_new_token(void)
{
    /* Leaf lock: never calls into an engine/backend while holding it. */
    present_lock_enter(&token_lock, false);
    uint64_t value = 0;
    if (next_token != UINT64_MAX) {
        value = next_token++;
    }
    present_lock_exit(&token_lock, false);
    return value;
}

bool present_lock_init(present_lock_t *lock)
{
#ifdef PRESENT_HOST
    return pthread_mutex_init(lock, NULL) == 0;
#else
    *lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return true;
#endif
}

void present_lock_destroy(present_lock_t *lock)
{
#ifdef PRESENT_HOST
    pthread_mutex_destroy(lock);
#else
    (void)lock;
#endif
}

void PRESENT_ISR_ATTR present_lock_enter(present_lock_t *lock, bool isr)
{
#ifdef PRESENT_HOST
    (void)isr;
    pthread_mutex_lock(lock);
#else
    if (isr) {
        portENTER_CRITICAL_ISR(lock);
    } else {
        portENTER_CRITICAL(lock);
    }
#endif
}

void PRESENT_ISR_ATTR present_lock_exit(present_lock_t *lock, bool isr)
{
#ifdef PRESENT_HOST
    (void)isr;
    pthread_mutex_unlock(lock);
#else
    if (isr) {
        portEXIT_CRITICAL_ISR(lock);
    } else {
        portEXIT_CRITICAL(lock);
    }
#endif
}

void *present_alloc(size_t size)
{
#ifdef PRESENT_HOST
    return calloc(1, size);
#else
    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}

void *present_alloc_pixels(size_t size)
{
    if (size > SIZE_MAX - (PRESENT_BUFFER_ALIGNMENT - 1)) {
        return NULL;
    }
#ifdef PRESENT_HOST
    return aligned_alloc(PRESENT_BUFFER_ALIGNMENT, (size + PRESENT_BUFFER_ALIGNMENT - 1) & ~(size_t)(PRESENT_BUFFER_ALIGNMENT - 1));
#else
    return heap_caps_aligned_alloc(PRESENT_BUFFER_ALIGNMENT, (size + PRESENT_BUFFER_ALIGNMENT - 1) & ~(size_t)(PRESENT_BUFFER_ALIGNMENT - 1),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
}

void *present_alloc_pixels_in(size_t size, bool external)
{
#ifdef PRESENT_HOST
    (void)external;
    return present_alloc_pixels(size);
#else
    if (!external) {
        return present_alloc_pixels(size);
    }
    if (size > SIZE_MAX - (PRESENT_BUFFER_ALIGNMENT - 1)) {
        return NULL;
    }
    return heap_caps_aligned_alloc(PRESENT_BUFFER_ALIGNMENT,
                                   (size + PRESENT_BUFFER_ALIGNMENT - 1) & ~(size_t)(PRESENT_BUFFER_ALIGNMENT - 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
}

void present_free(void *memory)
{
    free(memory);
}

uint64_t PRESENT_ISR_ATTR present_now_us(void)
{
#ifdef PRESENT_HOST
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
#else
    return (uint64_t)esp_timer_get_time();
#endif
}

uint64_t present_now_ms(void)
{
    return present_now_us() / 1000;
}

void present_pause(void)
{
#ifdef PRESENT_HOST
    const struct timespec delay = { .tv_nsec = 1000000 };
    nanosleep(&delay, NULL);
#else
    vTaskDelay(1);
#endif
}

bool present_expired(uint64_t start, uint32_t timeout_ms)
{
    return timeout_ms != UINT32_MAX && present_now_ms() - start >= timeout_ms;
}

present_task_t present_current_task(void)
{
#ifdef PRESENT_HOST
    return pthread_self();
#else
    return xTaskGetCurrentTaskHandle();
#endif
}

bool present_same_task(present_task_t a, present_task_t b)
{
#ifdef PRESENT_HOST
    return pthread_equal(a, b) != 0;
#else
    return a == b;
#endif
}

#if !defined(PRESENT_HOST) && CONFIG_ESP_IPC_ENABLE
static void isr_barrier(void *unused)
{
    (void)unused;
}
#endif
esp_err_t present_isr_barrier(uint32_t timeout)
{
#ifdef PRESENT_HOST
    (void)timeout;
    /* Host event producers have no preempted ISR; their callback joins remain
     * the mock driver's responsibility. Active calls are counted by adapter. */
    return ESP_OK;
#else
#if CONFIG_ESP_IPC_ENABLE
    uint64_t start = present_now_ms();
    for (unsigned core = 0; core < portNUM_PROCESSORS; ++core) {
        if (present_expired(start, timeout)) {
            return ESP_ERR_TIMEOUT;
        }
        /* Include this CPU too: callers may migrate between iterations. A task
         * on that CPU cannot run until an ISR holding an old user_ctx exits. */
        esp_err_t error = esp_ipc_call_blocking(core, isr_barrier, NULL);
        if (error != ESP_OK) {
            return error;
        }
    }
    return present_expired(start, timeout) ? ESP_ERR_TIMEOUT : ESP_OK;
#else
#if CONFIG_FREERTOS_UNICORE
    (void)timeout;
    return ESP_OK;
#else
    (void)timeout;
    return ESP_ERR_NOT_SUPPORTED;
#endif
#endif
#endif
}
