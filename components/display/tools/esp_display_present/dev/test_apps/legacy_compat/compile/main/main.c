/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
/* Link-only consumer: intentionally no board setup or valid display config. */
#include "esp_display_present.h"
#include "esp_display_present_geometry.h"
void app_main(void)
{
    esp_display_presenter_t *p = 0;
    esp_display_presenter_config_t config = {0};
    (void)esp_display_presenter_create(&config, &p);
    esp_display_present_area_t area = {0, 0, 1, 1}, out;
    esp_display_present_render_alignment_t alignment = {0};
    (void)esp_display_present_geometry_align_area((esp_display_present_size_t) {
        2, 2
    }, &area, &alignment, &out);
}
