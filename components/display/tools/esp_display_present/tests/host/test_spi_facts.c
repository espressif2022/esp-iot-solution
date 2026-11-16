/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "support/legacy_panel.h"
static void check_size(unsigned side)
{
    for (unsigned driver = 0; driver < 2; ++driver) {
        for (unsigned declared = 0; declared < 2; ++declared) {
            struct fake_lcd panel;
            init_panel(&panel);
            panel.psram_direct = driver;
            esp_display_presenter_config_t input = config(&panel, false);
            input.width = input.height = side;
            input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
            input.target.hw.io = &panel;
            input.target.hw.spi_psram_dma_direct = declared;
            input.target.hw.te_enabled = true;
            input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
                .gpio_num = 1, .bus_freq_hz = 40000000, .data_lines = 4
            };
            input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
            present_resources_t *resources = NULL;
            esp_err_t error = present_resources_create(&input, false, NULL, NULL, &resources);
#ifdef PRESENT_HOST_SPI_NO_MEMORY_INFO
            bool supported = declared;
#else
            bool supported = driver;
#endif
            assert(error == ESP_OK);
            present_resource_status_t status;
            OK(present_resources_status(resources, &status));
            assert(status.plan.output.ext_mem);
            assert(status.plan.output.ext_len_align == (supported ? 64 : 1));
            assert(panel.calls == 0);
            if (resources) {
                OK(present_resources_destroy(resources, 1000));
            }
            free_panel(&panel);
        }
    }
}

int main(void)
{
    check_size(8); /* Both direct and bounded dual-stage transport accept a tiny frame. */
    check_size(480);
    return 0;
}
