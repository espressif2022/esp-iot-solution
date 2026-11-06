/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "present_backend.h"
#include "present_pixel_engine.h"
#include "present_lcd_spi.h"
#include "present_presenter.h"
#include "present_executor.h"
#include "present_plan.h"
#include "present_mode.h"
#include "present_lcd_scanout.h"
#include "present_transfer_tracker.h"
#include "present_completion_tracker.h"
int main()
{
    esp_display_present_pixel_engine_t *engine = nullptr;
    esp_display_present_pixel_config_t config{};
    config.max_jobs = 1;
    if (esp_display_present_pixel_create(&config, &engine) != ESP_OK) {
        return 1;
    }
    if (esp_display_present_pixel_stop(engine, 0) != ESP_OK) {
        return 2;
    }
    return esp_display_present_pixel_destroy(engine) != ESP_OK;
}
