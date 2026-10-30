/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_os.h"
#include "present_limits.h"
#include <stdlib.h>
#ifdef PV2_HOST
#include <time.h>
#else
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/task.h"
#endif

#ifdef PV2_HOST
static pv2_lock_t token_lock = PTHREAD_MUTEX_INITIALIZER;
#else
static pv2_lock_t token_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static uint64_t next_token = 1;

uint64_t pv2_new_token(void)
{
    /* Leaf lock: never calls into an engine/backend while holding it. */
    pv2_lock_enter(&token_lock, false);
    uint64_t value = 0;
    if (next_token != UINT64_MAX) {
        value = next_token++;
    }
    pv2_lock_exit(&token_lock, false);
    return value;
}

bool pv2_lock_init(pv2_lock_t *lock)
{
#ifdef PV2_HOST
    return pthread_mutex_init(lock, NULL) == 0;
#else
    *lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    return true;
#endif
}

void pv2_lock_destroy(pv2_lock_t *lock)
{
#ifdef PV2_HOST
    pthread_mutex_destroy(lock);
#else
    (void)lock;
#endif
}

void PV2_ISR_ATTR pv2_lock_enter(pv2_lock_t *lock, bool isr)
{
#ifdef PV2_HOST
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

void PV2_ISR_ATTR pv2_lock_exit(pv2_lock_t *lock, bool isr)
{
#ifdef PV2_HOST
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

void *pv2_alloc(size_t size)
{
#ifdef PV2_HOST
    return calloc(1, size);
#else
    return heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}

void *pv2_alloc_pixels(size_t size)
{
    if (size > SIZE_MAX - (PV2_BUFFER_ALIGNMENT - 1)) {
        return NULL;
    }
#ifdef PV2_HOST
    return aligned_alloc(PV2_BUFFER_ALIGNMENT, (size + PV2_BUFFER_ALIGNMENT - 1) & ~(size_t)(PV2_BUFFER_ALIGNMENT - 1));
#else
    return heap_caps_aligned_alloc(PV2_BUFFER_ALIGNMENT, (size + PV2_BUFFER_ALIGNMENT - 1) & ~(size_t)(PV2_BUFFER_ALIGNMENT - 1),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
#endif
}

void pv2_free(void *memory)
{
    free(memory);
}

uint64_t pv2_now_us(void)
{
#ifdef PV2_HOST
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
#else
    return (uint64_t)esp_timer_get_time();
#endif
}

uint64_t pv2_now_ms(void)
{
    return pv2_now_us() / 1000;
}

void pv2_pause(void)
{
#ifdef PV2_HOST
    const struct timespec delay = { .tv_nsec = 1000000 };
    nanosleep(&delay, NULL);
#else
    vTaskDelay(1);
#endif
}

bool pv2_expired(uint64_t start, uint32_t timeout_ms)
{
    return timeout_ms != UINT32_MAX && pv2_now_ms() - start >= timeout_ms;
}

pv2_task_t pv2_current_task(void)
{
#ifdef PV2_HOST
    return pthread_self();
#else
    return xTaskGetCurrentTaskHandle();
#endif
}

bool pv2_same_task(pv2_task_t a, pv2_task_t b)
{
#ifdef PV2_HOST
    return pthread_equal(a, b) != 0;
#else
    return a == b;
#endif
}
