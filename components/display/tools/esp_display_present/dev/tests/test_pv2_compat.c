/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Source compatibility for the old extension API and stable application API. */
#include "esp_display_present.h"
#include "present_pv2_compat.h"
int main(void)
{
    pv2_presenter_t *core = NULL;
    esp_display_presenter_t *legacy = NULL;
    pv2_presenter_config_t config = {0};
    (void)legacy;
    (void)config;
    return pv2_presenter_create(NULL, &core) != ESP_ERR_INVALID_ARG;
}
