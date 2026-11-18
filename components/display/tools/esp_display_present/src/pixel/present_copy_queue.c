/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "pixel/present_copy_queue.h"
#include "pixel/present_pixel_ops.h"
#include "resources/present_memory.h"
#include "platform/present_os.h"
#include "platform/present_limits.h"

typedef enum {
    COPY_SLOT_FREE = 0,
    COPY_SLOT_QUEUED,
    COPY_SLOT_RUNNING,
    COPY_SLOT_DONE,
} copy_slot_state_t;

typedef struct {
    copy_slot_state_t state;
    bool canceled;
    esp_err_t result;
    uint64_t seq;
    esp_display_present_copy_request_t request;
} copy_slot_t;

struct present_copy_queue {
    present_lock_t lock;
    bool closing;
    bool stopped;
    bool executing;
    bool external;
    struct {
        bool initialized;
        bool applied;
        unsigned value;
    } priority;
    struct {
        bool active;
        present_task_t task;
    } callback;
    present_os_event_t *work;
    present_os_event_t *idle;
    uint32_t notifying;
    present_worker_t *task;
    present_copy_work_fn on_work;
    present_copy_done_fn on_done;
    esp_display_present_pixel_execute_fn execute;
    void *work_user;
    void *done_user;
    void *execute_user;
    uint64_t next_seq;
    size_t count;
    copy_slot_t slots[];
};

static copy_slot_t *slot_for(present_copy_queue_t *queue, present_copy_handle_t handle)
{
    if (!queue || !handle.slot || handle.slot > queue->count) {
        return NULL;
    }
    return &queue->slots[handle.slot - 1];
}

static void wake_worker(present_copy_queue_t *queue)
{
    if (queue->external) {
        queue->on_work(queue->work_user);
    } else {
        (void)present_event_signal(queue->work, false);
    }
}

static bool execute_next(present_copy_queue_t *queue)
{
    present_lock_enter(&queue->lock, false);
    if (queue->executing) {
        present_lock_exit(&queue->lock, false);
        return false;
    }
    copy_slot_t *next = NULL;
    for (size_t i = 0; i < queue->count; ++i) {
        copy_slot_t *slot = &queue->slots[i];
        if (slot->state == COPY_SLOT_QUEUED && (!next || slot->seq < next->seq)) {
            next = slot;
        }
    }
    if (!next) {
        present_lock_exit(&queue->lock, false);
        return false;
    }
    bool canceled = next->canceled;
    bool set_priority = !queue->external && !queue->priority.applied;
    unsigned priority = queue->priority.value;
    queue->priority.applied = true;
    next->state = COPY_SLOT_RUNNING;
    queue->executing = true;
    esp_display_present_copy_request_t request = next->request;
    present_copy_handle_t handle = {(uint8_t)(next - queue->slots + 1)};
    present_lock_exit(&queue->lock, false);

    if (set_priority) {
        present_task_set_priority(priority);
    }
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!canceled) {
        result = queue->execute ? queue->execute(queue->execute_user, &request)
                 : esp_display_present_pixel_execute_cpu(&request);
    }

    present_lock_enter(&queue->lock, false);
    next->result = result;
    next->state = COPY_SLOT_DONE;
    present_copy_done_fn on_done = queue->on_done;
    void *done_user = queue->done_user;
    if (on_done) {
        queue->callback.active = true;
        queue->callback.task = present_current_task();
    }
    present_lock_exit(&queue->lock, false);

    if (on_done) {
        (void)on_done(done_user, handle, result, false);
    }

    present_lock_enter(&queue->lock, false);
    queue->callback.active = false;
    queue->executing = false;
    bool signal_idle = queue->closing && queue->idle != NULL;
    if (signal_idle) {
        queue->notifying++;
    }
    present_lock_exit(&queue->lock, false);

    if (signal_idle) {
        (void)present_event_signal(queue->idle, false);
        present_lock_enter(&queue->lock, false);
        queue->notifying--;
        present_lock_exit(&queue->lock, false);
    }
    return true;
}

static void copy_worker(void *context)
{
    present_copy_queue_t *queue = context;
    for (;;) {
        if (execute_next(queue)) {
            continue;
        }
        present_lock_enter(&queue->lock, false);
        bool closing = queue->closing;
        present_lock_exit(&queue->lock, false);
        if (closing) {
            if (execute_next(queue)) {
                continue;
            }
            return;
        }
        (void)present_event_wait(queue->work, UINT32_MAX);
    }
}

static void free_queue(present_copy_queue_t *queue)
{
    present_worker_destroy(queue->task);
    present_event_destroy(queue->work);
    present_event_destroy(queue->idle);
    present_lock_destroy(&queue->lock);
    present_free(queue);
}

esp_err_t present_copy_queue_create(const present_copy_queue_config_t *config, present_copy_queue_t **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!config || !config->max_jobs || config->max_jobs > PRESENT_MAX_COPY_JOBS ||
            config->max_jobs > UINT8_MAX || (config->external_worker && !config->on_work)) {
        return ESP_ERR_INVALID_ARG;
    }
    present_copy_queue_t *queue = present_alloc(sizeof(*queue) + config->max_jobs * sizeof(copy_slot_t));
    if (!queue) {
        return ESP_ERR_NO_MEM;
    }
    if (!present_lock_init(&queue->lock)) {
        present_free(queue);
        return ESP_ERR_NO_MEM;
    }
    queue->count = config->max_jobs;
    queue->external = config->external_worker;
    queue->on_work = config->on_work;
    queue->on_done = config->on_done;
    queue->execute = config->execute;
    queue->work_user = config->work_user;
    queue->done_user = config->done_user;
    queue->execute_user = config->execute_user;
    if (queue->external) {
        queue->idle = present_event_create();
        if (!queue->idle) {
            free_queue(queue);
            return ESP_ERR_NO_MEM;
        }
    } else {
        queue->work = present_event_create();
        if (!queue->work) {
            free_queue(queue);
            return ESP_ERR_NO_MEM;
        }
        queue->task = present_worker_create("present_pixel", copy_worker, queue, 4096, 5);
        if (!queue->task) {
            free_queue(queue);
            return ESP_ERR_NO_MEM;
        }
    }
    *out = queue;
    return ESP_OK;
}

esp_err_t present_copy_queue_submit(present_copy_queue_t *queue, const esp_display_present_copy_request_t *request,
                                    present_copy_handle_t *out_handle)
{
    if (!out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    out_handle->slot = 0;
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = esp_display_present_pixel_validate_request(request);
    if (result != ESP_OK) {
        return result;
    }
    unsigned priority = 0;
    bool have_priority = false;
    copy_slot_t *slot = NULL;
    size_t idx = 0;
    present_lock_enter(&queue->lock, false);
    for (;;) {
        if (queue->closing) {
            present_lock_exit(&queue->lock, false);
            return ESP_ERR_INVALID_STATE;
        }
        slot = NULL;
        for (size_t i = 0; i < queue->count; ++i) {
            if (queue->slots[i].state == COPY_SLOT_FREE) {
                slot = &queue->slots[i];
                idx = i;
                break;
            }
        }
        if (!slot) {
            present_lock_exit(&queue->lock, false);
            return ESP_ERR_NO_MEM;
        }
        if (!queue->external && !queue->priority.initialized && !have_priority) {
            present_lock_exit(&queue->lock, false);
            priority = present_task_priority();
            have_priority = true;
            present_lock_enter(&queue->lock, false);
            continue;
        }
        if (!queue->external && !queue->priority.initialized) {
            queue->priority.value = priority;
            queue->priority.initialized = true;
        }
        slot->state = COPY_SLOT_QUEUED;
        slot->canceled = false;
        slot->result = ESP_OK;
        slot->seq = ++queue->next_seq;
        slot->request = *request;
        out_handle->slot = (uint8_t)(idx + 1);
        break;
    }
    present_lock_exit(&queue->lock, false);
    wake_worker(queue);
    return ESP_OK;
}

bool present_copy_queue_run_one(present_copy_queue_t *queue)
{
    return queue && queue->external && execute_next(queue);
}

esp_err_t present_copy_queue_take(present_copy_queue_t *queue, present_copy_handle_t handle, esp_err_t *out_result)
{
    if (!queue || !out_result || !handle.slot || handle.slot > queue->count) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&queue->lock, false);
    copy_slot_t *slot = &queue->slots[handle.slot - 1];
    if (slot->state == COPY_SLOT_FREE) {
        present_lock_exit(&queue->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (slot->state != COPY_SLOT_DONE) {
        present_lock_exit(&queue->lock, false);
        return ESP_ERR_TIMEOUT;
    }
    *out_result = slot->result;
    slot->state = COPY_SLOT_FREE;
    present_lock_exit(&queue->lock, false);
    return ESP_OK;
}

esp_err_t present_copy_queue_take_next(present_copy_queue_t *queue,
                                       esp_display_present_copy_request_t *out_request, esp_err_t *out_result)
{
    if (!queue || !out_request || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&queue->lock, false);
    copy_slot_t *next = NULL;
    for (size_t i = 0; i < queue->count; ++i) {
        copy_slot_t *slot = &queue->slots[i];
        if (slot->state == COPY_SLOT_DONE && (!next || slot->seq < next->seq)) {
            next = slot;
        }
    }
    if (next) {
        *out_request = next->request;
        *out_result = next->result;
        next->state = COPY_SLOT_FREE;
    }
    present_lock_exit(&queue->lock, false);
    return next ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool present_copy_queue_source_busy(present_copy_queue_t *queue, const void *pixels,
                                    const esp_display_present_rect_t *area)
{
    if (!queue || !pixels) {
        return false;
    }
    present_lock_enter(&queue->lock, false);
    bool busy = false;
    for (size_t i = 0; i < queue->count; ++i) {
        const copy_slot_t *slot = &queue->slots[i];
        const esp_display_present_rect_t *read = &slot->request.src_area;
        if ((slot->state == COPY_SLOT_QUEUED || slot->state == COPY_SLOT_RUNNING) &&
                slot->request.src.pixels == pixels && (!area ||
                                                       ((uint64_t)area->x < (uint64_t)read->x + read->width &&
                                                        (uint64_t)read->x < (uint64_t)area->x + area->width &&
                                                        (uint64_t)area->y < (uint64_t)read->y + read->height &&
                                                        (uint64_t)read->y < (uint64_t)area->y + area->height))) {
            busy = true;
            break;
        }
    }
    present_lock_exit(&queue->lock, false);
    return busy;
}

esp_err_t present_copy_queue_cancel(present_copy_queue_t *queue, present_copy_handle_t handle, esp_err_t *out_result)
{
    if (!queue || !out_result || !handle.slot || handle.slot > queue->count) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&queue->lock, false);
    copy_slot_t *slot = slot_for(queue, handle);
    if (slot->state == COPY_SLOT_FREE) {
        present_lock_exit(&queue->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    if (slot->state == COPY_SLOT_DONE) {
        *out_result = slot->result;
        slot->state = COPY_SLOT_FREE;
        present_lock_exit(&queue->lock, false);
        return ESP_OK;
    }
    bool need_wake = false;
    if (slot->state == COPY_SLOT_QUEUED) {
        slot->canceled = true;
        need_wake = true;
    }
    present_lock_exit(&queue->lock, false);
    if (need_wake) {
        wake_worker(queue);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t present_copy_queue_stop(present_copy_queue_t *queue, uint32_t timeout_ms)
{
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t start = present_now_ms();
    present_lock_enter(&queue->lock, false);
    if (queue->callback.active && present_same_task(queue->callback.task, present_current_task())) {
        present_lock_exit(&queue->lock, false);
        return ESP_ERR_INVALID_STATE;
    }
    queue->closing = true;
    if (queue->external) {
        for (size_t i = 0; i < queue->count; ++i) {
            if (queue->slots[i].state == COPY_SLOT_QUEUED) {
                queue->slots[i].canceled = true;
            }
        }
    }
    present_lock_exit(&queue->lock, false);
    if (!queue->external) {
        wake_worker(queue);
        if (!present_worker_join(queue->task, timeout_ms)) {
            return ESP_ERR_TIMEOUT;
        }
    } else {
        for (;;) {
            if (execute_next(queue)) {
                continue;
            }
            present_lock_enter(&queue->lock, false);
            bool busy = queue->executing;
            bool notifying = queue->notifying != 0;
            present_lock_exit(&queue->lock, false);
            if (!busy && !notifying) {
                break;
            }
            uint64_t elapsed = present_now_ms() - start;
            if (timeout_ms != UINT32_MAX && elapsed >= timeout_ms) {
                return ESP_ERR_TIMEOUT;
            }
            if (busy) {
                (void)present_event_wait(queue->idle, timeout_ms == UINT32_MAX ? UINT32_MAX : timeout_ms - (uint32_t)elapsed);
            } else {
                present_pause();
            }
        }
    }
    present_lock_enter(&queue->lock, false);
    queue->stopped = true;
    present_lock_exit(&queue->lock, false);
    return ESP_OK;
}

esp_err_t present_copy_queue_destroy(present_copy_queue_t *queue)
{
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }
    present_lock_enter(&queue->lock, false);
    bool can_destroy = queue->stopped && !queue->executing && !queue->notifying;
    for (size_t i = 0; can_destroy && i < queue->count; ++i) {
        if (queue->slots[i].state != COPY_SLOT_FREE) {
            can_destroy = false;
        }
    }
    present_lock_exit(&queue->lock, false);
    if (!can_destroy) {
        return ESP_ERR_INVALID_STATE;
    }
    free_queue(queue);
    return ESP_OK;
}

size_t present_copy_queue_memory(size_t jobs, bool dedicated_worker)
{
    return sizeof(present_copy_queue_t) + jobs * sizeof(copy_slot_t) + present_event_memory() +
           (dedicated_worker ? present_worker_memory() : 0);
}
