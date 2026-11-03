/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_memory.h"
#include "present_os.h"
#ifdef PRESENT_HOST
#include <errno.h>
#include <time.h>
#else
#include "freertos/semphr.h"
#endif

struct present_os_event {
#ifdef PRESENT_HOST
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
#else
    StaticSemaphore_t storage;
    SemaphoreHandle_t semaphore;
#endif
};
struct present_mutex {
#ifdef PRESENT_HOST
    present_os_event_t *available;
#else
    StaticSemaphore_t storage;
    SemaphoreHandle_t semaphore;
#endif
};
struct present_worker {
    present_worker_entry_fn entry;
    void *user;
    present_os_event_t *exited;
    bool joined;
#ifdef PRESENT_HOST
    pthread_t thread;
#else
    TaskHandle_t task;
#endif
};
#ifndef PRESENT_HOST
static TickType_t timeout_ticks(uint32_t timeout)
{
    if (timeout == UINT32_MAX) {
        return portMAX_DELAY;
    }
    if (!timeout) {
        return 0;
    }
    uint64_t ticks = ((uint64_t)timeout * configTICK_RATE_HZ + 999) / 1000;
    return ticks >= portMAX_DELAY ? portMAX_DELAY - 1 : (TickType_t)ticks;
}
#endif
present_os_event_t *present_event_create(void)
{
    present_os_event_t *event = present_alloc(sizeof(*event));
    if (!event) {
        return NULL;
    }
#ifdef PRESENT_HOST
    if (pthread_mutex_init(&event->mutex, NULL) != 0) {
        present_free(event);
        return NULL;
    }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&event->mutex);
        present_free(event);
        return NULL;
    }
    int result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!result) {
        result = pthread_cond_init(&event->condition, &attr);
    }
    pthread_condattr_destroy(&attr);
    if (result) {
        pthread_mutex_destroy(&event->mutex);
        present_free(event);
        return NULL;
    }
#else
    event->semaphore = xSemaphoreCreateBinaryStatic(&event->storage);
    if (!event->semaphore) {
        present_free(event);
        return NULL;
    }
#endif
    return event;
}

bool PRESENT_ISR_ATTR present_event_signal(present_os_event_t *event, bool isr)
{
    if (!event) {
        return false;
    }
#ifdef PRESENT_HOST
    (void)isr;
    pthread_mutex_lock(&event->mutex);
    event->signaled = true;
    pthread_cond_signal(&event->condition);
    pthread_mutex_unlock(&event->mutex);
    return false;
#else
    if (isr) {
        BaseType_t yield = pdFALSE;
        xSemaphoreGiveFromISR(event->semaphore, &yield);
        return yield == pdTRUE;
    }
    xSemaphoreGive(event->semaphore);
    return false;
#endif
}

bool present_event_wait(present_os_event_t *event, uint32_t timeout)
{
    if (!event) {
        return false;
    }
#ifdef PRESENT_HOST
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += (long)(timeout % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&event->mutex);
    int result = 0;
    while (!event->signaled && !result && timeout) {
        result = timeout == UINT32_MAX ? pthread_cond_wait(&event->condition, &event->mutex) :
                 pthread_cond_timedwait(&event->condition, &event->mutex, &deadline);
    }
    bool signaled = event->signaled;
    if (signaled) {
        event->signaled = false;
    }
    pthread_mutex_unlock(&event->mutex);
    return signaled;
#else
    return xSemaphoreTake(event->semaphore, timeout_ticks(timeout)) == pdTRUE;
#endif
}

void present_event_destroy(present_os_event_t *event)
{
    if (!event) {
        return;
    }
#ifdef PRESENT_HOST
    pthread_cond_destroy(&event->condition);
    pthread_mutex_destroy(&event->mutex);
#else
    vSemaphoreDelete(event->semaphore);
#endif
    present_free(event);
}

present_mutex_t *present_mutex_create(void)
{
    present_mutex_t *mutex = present_alloc(sizeof(*mutex));
    if (!mutex) {
        return NULL;
    }
#ifdef PRESENT_HOST
    mutex->available = present_event_create();
    if (!mutex->available) {
        present_free(mutex);
        return NULL;
    }
    (void)present_event_signal(mutex->available, false);
#else
    mutex->semaphore = xSemaphoreCreateMutexStatic(&mutex->storage);
    if (!mutex->semaphore) {
        present_free(mutex);
        return NULL;
    }
#endif
    return mutex;
}

bool present_mutex_take(present_mutex_t *mutex, uint32_t timeout)
{
#ifdef PRESENT_HOST
    return present_event_wait(mutex->available, timeout);
#else
    return xSemaphoreTake(mutex->semaphore, timeout_ticks(timeout)) == pdTRUE;
#endif
}

void present_mutex_give(present_mutex_t *mutex)
{
#ifdef PRESENT_HOST
    (void)present_event_signal(mutex->available, false);
#else
    xSemaphoreGive(mutex->semaphore);
#endif
}

void present_mutex_destroy(present_mutex_t *mutex)
{
    if (!mutex) {
        return;
    }
#ifdef PRESENT_HOST
    present_event_destroy(mutex->available);
#else
    vSemaphoreDelete(mutex->semaphore);
#endif
    present_free(mutex);
}
#ifdef PRESENT_HOST
static void *worker_entry(void *context)
#else
static void worker_entry(void *context)
#endif
{
    present_worker_t *worker = context;
    worker->entry(worker->user);
    /* No access to worker or user after publishing exit. Host join additionally
     * waits for pthread return; FreeRTOS self-delete only touches its own TCB. */
    (void)present_event_signal(worker->exited, false);
#ifdef PRESENT_HOST
    return NULL;
#else
    vTaskDelete(NULL);
#endif
}

present_worker_t *present_worker_create(present_worker_entry_fn entry, void *user, uint32_t stack_bytes, unsigned priority)
{
    if (!entry) {
        return NULL;
    }
    present_worker_t *worker = present_alloc(sizeof(*worker));
    if (!worker) {
        return NULL;
    }
    worker->entry = entry;
    worker->user = user;
    worker->exited = present_event_create();
    if (!worker->exited) {
        present_free(worker);
        return NULL;
    }
#ifdef PRESENT_HOST
    (void)stack_bytes;
    (void)priority;
    bool created = pthread_create(&worker->thread, NULL, worker_entry, worker) == 0;
#else
    bool created = priority < configMAX_PRIORITIES &&
                   xTaskCreate(worker_entry, "present_service", stack_bytes, worker, priority, &worker->task) == pdPASS;
#endif
    if (!created) {
        present_event_destroy(worker->exited);
        present_free(worker);
        return NULL;
    }
    return worker;
}

bool present_worker_join(present_worker_t *worker, uint32_t timeout)
{
    if (worker->joined) {
        return true;
    }
    if (!present_event_wait(worker->exited, timeout)) {
        return false;
    }
#ifdef PRESENT_HOST
    if (pthread_join(worker->thread, NULL) != 0) {
        return false;
    }
#endif
    worker->joined = true;
    return true;
}

void present_worker_destroy(present_worker_t *worker)
{
    if (!worker || !worker->joined) {
        return;
    }
    present_event_destroy(worker->exited);
    present_free(worker);
}

size_t present_event_memory(void)
{
    return sizeof(present_os_event_t);
}
size_t present_mutex_memory(void)
{
    size_t bytes = sizeof(present_mutex_t);
#ifdef PRESENT_HOST
    bytes += sizeof(present_os_event_t);
#endif
    return bytes;
}
size_t present_worker_memory(void)
{
    size_t bytes = sizeof(present_worker_t) + sizeof(present_os_event_t);
#ifndef PRESENT_HOST
    bytes += sizeof(StaticTask_t);
#endif
    return bytes;
}
