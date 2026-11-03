/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
/* Link-only consumer: intentionally no board setup or valid display config. */
#include "esp_display_present.h"
void app_main(void)
{
    esp_display_presenter_t *p = 0;
    esp_display_presenter_config_t config = {0};
    (void)esp_display_presenter_create(&config, &p);
}
