/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Source compatibility of the native LVGL binding. */
#include "present_lvgl_pv2_compat.h"
int main(void)
{
    pv2_lvgl_t *binding = NULL;
    return pv2_lvgl_create(NULL, 0, &binding) != ESP_ERR_INVALID_ARG;
}
