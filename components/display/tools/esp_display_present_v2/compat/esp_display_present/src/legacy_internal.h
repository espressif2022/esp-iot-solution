/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_display_present.h"
#include "present_presenter.h"
/* Exclusive renderer session. Bind/Unbind run on the LVGL task, outside any
 * LVGL callback. A bound session blocks legacy producer and stop/delete APIs. */
esp_err_t pv2_legacy_bind(esp_display_presenter_t *, pv2_presenter_t **);
void pv2_legacy_unbind(esp_display_presenter_t *);

uint64_t pv2_legacy_committed(esp_display_presenter_t *);
