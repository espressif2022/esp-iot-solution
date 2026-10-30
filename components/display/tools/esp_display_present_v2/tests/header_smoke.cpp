/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_pixel_engine.h"
#include "present_lcd_spi.h"
#include "present_presenter.h"
#include "present_pixel_accel.h"
#include "present_lcd_scanout.h"
int main()
{
    pv2_pixel_engine_t *engine = nullptr;
    pv2_pixel_config_t config{};
    config.max_jobs = 1;
    if (pv2_pixel_create(&config, &engine) != ESP_OK) {
        return 1;
    }
    if (pv2_pixel_stop(engine, 0) != ESP_OK) {
        return 2;
    }
    return pv2_pixel_destroy(engine) != ESP_OK || pv2_gram_backend.create == nullptr || pv2_te_backend.create == nullptr || pv2_fb_backend.create == nullptr;
}
