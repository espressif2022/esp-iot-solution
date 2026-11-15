/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gsp/gsp_decode_task.h"
#include "gsp/platform/esp_gsp_idf_accel.h"
#include <assert.h>

gsp_err_t gsp_decode_task_start(gsp_decode_task_t *task, gsp_image_cache_t *cache,
                                const gsp_decode_task_config_t *config)
{
    (void)task;
    (void)cache;
    (void)config;
    assert(false);
    return GSP_ERR_UNSUPPORTED;
}

void gsp_decode_task_kick(gsp_decode_task_t *task)
{
    (void)task;
    assert(false);
}

gsp_err_t gsp_decode_task_lock(gsp_decode_task_t *task)
{
    (void)task;
    assert(false);
    return GSP_ERR_UNSUPPORTED;
}

bool gsp_decode_task_try_lock(gsp_decode_task_t *task)
{
    (void)task;
    assert(false);
    return false;
}

void gsp_decode_task_unlock(gsp_decode_task_t *task)
{
    (void)task;
    assert(false);
}

gsp_err_t gsp_decode_task_set_cache_locked(gsp_decode_task_t *task, gsp_image_cache_t *cache)
{
    (void)task;
    (void)cache;
    assert(false);
    return GSP_ERR_UNSUPPORTED;
}

gsp_err_t gsp_decode_task_stop(gsp_decode_task_t *task)
{
    (void)task;
    assert(false);
    return GSP_ERR_UNSUPPORTED;
}

gsp_err_t esp_gsp_idf_accel_acquire_default(const esp_gsp_idf_accel_config_t *config)
{
    (void)config;
    assert(false);
    return GSP_ERR_UNSUPPORTED;
}

void esp_gsp_idf_accel_release_default(void)
{
    assert(false);
}

size_t esp_gsp_idf_accel_pixel_processors(gsp_pixel_processor_t *out, size_t capacity)
{
    (void)out;
    (void)capacity;
    assert(false);
    return 0;
}
