/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "esp_lv_present.h"
#include "present_lvgl.h"
#include "legacy_internal.h"
#ifndef PV2_HOST
#include "esp_timer.h"
static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}
#endif
/* As in V1, caller serializes the singleton and all LVGL access on its UI task. */
static pv2_lvgl_t *binding;
static esp_display_presenter_t *owner;
static uint64_t first_frame;

esp_err_t esp_lv_present_start(esp_display_presenter_t *presenter, lv_display_t **out)
{
    if (!presenter || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (binding) {
        return ESP_ERR_INVALID_STATE;
    }
    pv2_presenter_t *core;
    esp_err_t error = pv2_legacy_bind(presenter, &core);
    if (error != ESP_OK) {
        return error;
    }
    if (!lv_is_initialized()) {
        lv_init();
    }
#ifndef PV2_HOST
    lv_tick_set_cb(tick_ms);
#endif
    pv2_backend_caps_t caps;
    error = pv2_presenter_get_caps(core, &caps);
    if (error != ESP_OK) {
        pv2_legacy_unbind(presenter);
        return error;
    }
    if (caps.draw_layout.format == PV2_FORMAT_RGB565 && caps.draw_layout.byte_order == PV2_ORDER_NATIVE) {
        if (caps.contract == PV2_CONTRACT_PARTITION) {
            error = pv2_lvgl_create_pooled(core, caps.max_tile_rows, &binding);
        } else if (caps.contract == PV2_CONTRACT_FULL) {
            error = pv2_lvgl_create_zero_copy(core, &binding);
        } else {
            error = pv2_lvgl_create(core, caps.max_tile_rows, &binding);
        }
    } else {
        error = pv2_lvgl_create(core, caps.max_tile_rows, &binding);
    }
    if (error == ESP_ERR_NOT_SUPPORTED) {
        /* Layout rejection occurs before rendering/lease acquisition. */
        error = pv2_lvgl_create(core, caps.max_tile_rows, &binding);
    }
    if (error != ESP_OK) {
        pv2_legacy_unbind(presenter);
        return error;
    }
    first_frame = pv2_legacy_committed(presenter);
    owner = presenter;
    *out = pv2_lvgl_get_display(binding);
    return ESP_OK;
}
esp_err_t esp_lv_present_stop(void)
{
    if (!binding) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t error = pv2_lvgl_destroy(binding);
    if (error != ESP_OK) {
        return error;
    }
    binding = NULL;
    pv2_legacy_unbind(owner);
    owner = NULL;
    return ESP_OK;
}
uint32_t esp_lv_present_get_frame_count(void)
{
    if (!binding) {
        return 0;
    }
    uint64_t frames = pv2_legacy_committed(owner) - first_frame;
    return frames > UINT32_MAX ? UINT32_MAX : (uint32_t)frames;
}
