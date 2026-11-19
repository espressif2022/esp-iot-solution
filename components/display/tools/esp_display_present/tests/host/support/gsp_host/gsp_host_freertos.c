/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct host_queue {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    size_t head, count, capacity, item_size;
    unsigned char data[];
};

QueueHandle_t xQueueCreate(UBaseType_t capacity, UBaseType_t item_size)
{
    assert(capacity);
    QueueHandle_t queue = calloc(1, sizeof(*queue) + (size_t)capacity * item_size);
    assert(queue);
    queue->capacity = capacity;
    queue->item_size = item_size;
    assert(pthread_mutex_init(&queue->mutex, NULL) == 0);
    pthread_condattr_t attr;
    assert(pthread_condattr_init(&attr) == 0);
    assert(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0);
    assert(pthread_cond_init(&queue->changed, &attr) == 0);
    assert(pthread_condattr_destroy(&attr) == 0);
    return queue;
}

static struct timespec deadline(TickType_t wait)
{
    struct timespec result;
    assert(clock_gettime(CLOCK_MONOTONIC, &result) == 0);
    result.tv_sec += wait / 1000;
    result.tv_nsec += (wait % 1000) * 1000000L;
    result.tv_sec += result.tv_nsec / 1000000000L;
    result.tv_nsec %= 1000000000L;
    return result;
}

static bool wait_ready(QueueHandle_t queue, TickType_t wait, bool sending)
{
    const struct timespec end = deadline(wait);
    while (sending ? queue->count == queue->capacity : queue->count == 0) {
        if (!wait) {
            return false;
        }
        int result = wait == portMAX_DELAY ? pthread_cond_wait(&queue->changed, &queue->mutex) :
                     pthread_cond_timedwait(&queue->changed, &queue->mutex, &end);
        if (result == ETIMEDOUT) {
            return false;
        }
        assert(result == 0);
    }
    return true;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    pthread_mutex_lock(&queue->mutex);
    bool ready = wait_ready(queue, wait, true);
    if (ready) {
        if (queue->item_size) {
            size_t tail = (queue->head + queue->count) % queue->capacity;
            memcpy(queue->data + tail * queue->item_size, item, queue->item_size);
        }
        ++queue->count;
        pthread_cond_broadcast(&queue->changed);
    }
    pthread_mutex_unlock(&queue->mutex);
    return ready ? pdTRUE : pdFALSE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item, BaseType_t *woken)
{
    *woken = pdFALSE;
    return xQueueSend(queue, item, 0);
}

static BaseType_t read_queue(QueueHandle_t queue, void *item, TickType_t wait, bool remove)
{
    pthread_mutex_lock(&queue->mutex);
    bool ready = wait_ready(queue, wait, false);
    if (ready) {
        if (queue->item_size) {
            memcpy(item, queue->data + queue->head * queue->item_size, queue->item_size);
        }
        if (remove) {
            queue->head = (queue->head + 1) % queue->capacity;
            --queue->count;
            pthread_cond_broadcast(&queue->changed);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    return ready ? pdTRUE : pdFALSE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    return read_queue(queue, item, wait, true);
}

BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t wait)
{
    return read_queue(queue, item, wait, false);
}

void vQueueDelete(QueueHandle_t queue)
{
    assert(pthread_cond_destroy(&queue->changed) == 0);
    assert(pthread_mutex_destroy(&queue->mutex) == 0);
    free(queue);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return xQueueCreate(1, 0);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    assert(xSemaphoreGive(sem) == pdTRUE);
    return sem;
}

struct host_task {
    pthread_t thread;
    TaskFunction_t entry;
    void *arg;
};
static _Thread_local TaskHandle_t current_task;

static void *task_entry(void *opaque)
{
    current_task = opaque;
    current_task->entry(current_task->arg);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, TaskHandle_t *out)
{
    (void)name;
    (void)stack;
    (void)priority;
    TaskHandle_t task = calloc(1, sizeof(*task));
    assert(task);
    task->entry = entry;
    task->arg = arg;
    *out = task;
    assert(pthread_create(&task->thread, NULL, task_entry, task) == 0);
    return pdPASS;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return current_task;
}

void vTaskDelete(TaskHandle_t task)
{
    if (task == NULL) {
        /* Monolithic GSP workers delete themselves after signalling stop. */
        assert(current_task);
        assert(pthread_detach(pthread_self()) == 0);
        free(current_task);
        current_task = NULL;
        pthread_exit(NULL);
    }
    /* Split-adapter workers select the joinable shutdown path. */
    assert(task && task != current_task);
    assert(pthread_join(task->thread, NULL) == 0);
    free(task);
}

void vTaskSuspend(TaskHandle_t task)
{
    /* The GSP worker self-suspends at the end of its entry function. */
    assert(task == NULL && current_task != NULL);
}

int64_t esp_timer_get_time(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(esp_timer_get_time() / 1000);
}

void vTaskDelay(TickType_t ticks)
{
    const struct timespec end = deadline(ticks);
    int error;
    do {
        error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &end, NULL);
    } while (error == EINTR);
    assert(error == 0);
}
