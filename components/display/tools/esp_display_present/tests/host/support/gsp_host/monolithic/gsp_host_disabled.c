/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gsp/gsp_decode_task.h"
#include "gsp_esp_accel.h"
#include "gsp_esp_touch.h"
#include <assert.h>

gsp_err_t gsp_decode_task_start(gsp_decode_task_t *task, gsp_image_cache_t *cache,
                                const gsp_decode_task_config_t *config)
{
    (void)task;
    (void)cache;
    (void)config;
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
    return GSP_ERR_UNSUPPORTED;
}

gsp_err_t gsp_decode_task_stop(gsp_decode_task_t *task)
{
    (void)task;
    return GSP_ERR_UNSUPPORTED;
}

/* The Host runtime deliberately falls back to software rendering. */
gsp_err_t gsp_esp_accel_acquire_default(void)
{
    return GSP_ERR_UNSUPPORTED;
}

void gsp_esp_accel_release_default(void)
{
    assert(false);
}

void gsp_esp_touch_init(gsp_esp_touch_t *touch, esp_lcd_touch_handle_t handle,
                        bool interrupt_mode, bool interrupt_required)
{
    (void)interrupt_mode;
    (void)interrupt_required;
    assert(!handle);
    *touch = (gsp_esp_touch_t) {
        0
    };
}

esp_err_t gsp_esp_touch_start(gsp_esp_touch_t *touch,
                              gsp_esp_touch_wake_from_isr_t wake, void *user)
{
    (void)touch;
    (void)wake;
    (void)user;
    return ESP_OK;
}

void gsp_esp_touch_stop(gsp_esp_touch_t *touch)
{
    (void)touch;
}

esp_err_t gsp_esp_touch_poll(gsp_esp_touch_t *touch,
                             gsp_platform_pointer_t *out, bool *has_sample)
{
    (void)touch;
    (void)out;
    *has_sample = false;
    return ESP_OK;
}

esp_err_t gsp_esp_touch_poll_frame(gsp_esp_touch_t *touch,
                                   gsp_platform_touch_frame_t *out, bool *has_sample)
{
    (void)touch;
    (void)out;
    *has_sample = false;
    return ESP_OK;
}
