/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_os.h"
#ifdef PV2_HOST
#include <errno.h>
#include <time.h>
#else
#include "freertos/semphr.h"
#endif

struct pv2_event {
#ifdef PV2_HOST
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool signaled;
#else
    StaticSemaphore_t storage;
    SemaphoreHandle_t semaphore;
#endif
};
struct pv2_mutex {
#ifdef PV2_HOST
    pv2_event_t *available;
#else
    StaticSemaphore_t storage;
    SemaphoreHandle_t semaphore;
#endif
};
struct pv2_worker {
    pv2_worker_entry_fn entry;
    void *user;
    pv2_event_t *exited;
    bool joined;
#ifdef PV2_HOST
    pthread_t thread;
#else
    TaskHandle_t task;
#endif
};
#ifndef PV2_HOST
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
pv2_event_t *pv2_event_create(void)
{
    pv2_event_t *event = pv2_alloc(sizeof(*event));
    if (!event) {
        return NULL;
    }
#ifdef PV2_HOST
    if (pthread_mutex_init(&event->mutex, NULL) != 0) {
        pv2_free(event);
        return NULL;
    }
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&event->mutex);
        pv2_free(event);
        return NULL;
    }
    int result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!result) {
        result = pthread_cond_init(&event->condition, &attr);
    }
    pthread_condattr_destroy(&attr);
    if (result) {
        pthread_mutex_destroy(&event->mutex);
        pv2_free(event);
        return NULL;
    }
#else
    event->semaphore = xSemaphoreCreateBinaryStatic(&event->storage);
    if (!event->semaphore) {
        pv2_free(event);
        return NULL;
    }
#endif
    return event;
}

bool PV2_ISR_ATTR pv2_event_signal(pv2_event_t *event, bool isr)
{
    if (!event) {
        return false;
    }
#ifdef PV2_HOST
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

bool pv2_event_wait(pv2_event_t *event, uint32_t timeout)
{
    if (!event) {
        return false;
    }
#ifdef PV2_HOST
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

void pv2_event_destroy(pv2_event_t *event)
{
    if (!event) {
        return;
    }
#ifdef PV2_HOST
    pthread_cond_destroy(&event->condition);
    pthread_mutex_destroy(&event->mutex);
#else
    vSemaphoreDelete(event->semaphore);
#endif
    pv2_free(event);
}

pv2_mutex_t *pv2_mutex_create(void)
{
    pv2_mutex_t *mutex = pv2_alloc(sizeof(*mutex));
    if (!mutex) {
        return NULL;
    }
#ifdef PV2_HOST
    mutex->available = pv2_event_create();
    if (!mutex->available) {
        pv2_free(mutex);
        return NULL;
    }
    (void)pv2_event_signal(mutex->available, false);
#else
    mutex->semaphore = xSemaphoreCreateMutexStatic(&mutex->storage);
    if (!mutex->semaphore) {
        pv2_free(mutex);
        return NULL;
    }
#endif
    return mutex;
}

bool pv2_mutex_take(pv2_mutex_t *mutex, uint32_t timeout)
{
#ifdef PV2_HOST
    return pv2_event_wait(mutex->available, timeout);
#else
    return xSemaphoreTake(mutex->semaphore, timeout_ticks(timeout)) == pdTRUE;
#endif
}

void pv2_mutex_give(pv2_mutex_t *mutex)
{
#ifdef PV2_HOST
    (void)pv2_event_signal(mutex->available, false);
#else
    xSemaphoreGive(mutex->semaphore);
#endif
}

void pv2_mutex_destroy(pv2_mutex_t *mutex)
{
    if (!mutex) {
        return;
    }
#ifdef PV2_HOST
    pv2_event_destroy(mutex->available);
#else
    vSemaphoreDelete(mutex->semaphore);
#endif
    pv2_free(mutex);
}
#ifdef PV2_HOST
static void *worker_entry(void *context)
#else
static void worker_entry(void *context)
#endif
{
    pv2_worker_t *worker = context;
    worker->entry(worker->user);
    /* No access to worker or user after publishing exit. Host join additionally
     * waits for pthread return; FreeRTOS self-delete only touches its own TCB. */
    (void)pv2_event_signal(worker->exited, false);
#ifdef PV2_HOST
    return NULL;
#else
    vTaskDelete(NULL);
#endif
}

pv2_worker_t *pv2_worker_create(pv2_worker_entry_fn entry, void *user, uint32_t stack_bytes, unsigned priority)
{
    if (!entry) {
        return NULL;
    }
    pv2_worker_t *worker = pv2_alloc(sizeof(*worker));
    if (!worker) {
        return NULL;
    }
    worker->entry = entry;
    worker->user = user;
    worker->exited = pv2_event_create();
    if (!worker->exited) {
        pv2_free(worker);
        return NULL;
    }
#ifdef PV2_HOST
    (void)stack_bytes;
    (void)priority;
    bool created = pthread_create(&worker->thread, NULL, worker_entry, worker) == 0;
#else
    bool created = priority < configMAX_PRIORITIES &&
                   xTaskCreate(worker_entry, "pv2_service", stack_bytes, worker, priority, &worker->task) == pdPASS;
#endif
    if (!created) {
        pv2_event_destroy(worker->exited);
        pv2_free(worker);
        return NULL;
    }
    return worker;
}

bool pv2_worker_join(pv2_worker_t *worker, uint32_t timeout)
{
    if (worker->joined) {
        return true;
    }
    if (!pv2_event_wait(worker->exited, timeout)) {
        return false;
    }
#ifdef PV2_HOST
    if (pthread_join(worker->thread, NULL) != 0) {
        return false;
    }
#endif
    worker->joined = true;
    return true;
}

void pv2_worker_destroy(pv2_worker_t *worker)
{
    if (!worker || !worker->joined) {
        return;
    }
    pv2_event_destroy(worker->exited);
    pv2_free(worker);
}
