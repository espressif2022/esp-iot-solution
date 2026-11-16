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

static void region_direct_facts(void)
{
    static const struct {
        unsigned width, height, rows;
        bool direct[4];
    } cases[] = {
        {480, 480, 16, {false, false, false, false}},
        {480, 480, 32, {true, false, false, false}},
        {454, 480, 32, {false, false, false, false}},
        {454, 480, 64, {false, false, false, false}},
        {320, 480, 32, {false, false, false, false}},
        {800, 480, 32, {true, false, false, false}},
    };
    for (unsigned driver = 0; driver < 2; ++driver) {
        for (unsigned declared = 0; declared < 2; ++declared) {
            for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
                for (unsigned rotation = 0; rotation < 4; ++rotation) {
                    struct fake_lcd panel;
                    init_panel(&panel);
                    panel.psram_direct = driver;
                    esp_display_presenter_config_t input = config(&panel, false);
                    input.width = cases[c].width;
                    input.height = cases[c].height;
                    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
                    input.target.hw.io = &panel;
                    input.target.hw.spi_psram_dma_direct = declared;
                    input.target.hw.rotation = rotation * 90;
                    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
                    input.target.drawbuf.in_psram = true;
                    input.target.drawbuf.buffers = 1;
                    input.target.drawbuf.lines = cases[c].rows;
#ifdef PRESENT_HOST_SPI_NO_MEMORY_INFO
                    bool direct = declared && cases[c].direct[rotation];
#else
                    bool direct = driver && cases[c].direct[rotation];
#endif
                    present_resources_t *resources;
                    OK(present_resources_create(&input, false, NULL, NULL, &resources));
                    present_resource_status_t status;
                    OK(present_resources_status(resources, &status));
                    assert(status.plan.normalized.backend.tx_staging == !direct);
                    assert(status.plan.conversion.count == 1);
                    assert(status.plan.conversion.memory == (direct ? ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL : ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA));
                    assert(status.plan.stack_bytes == (direct && !rotation ? 0u : 8192u));
#ifdef PRESENT_HOST_SPI_NO_MEMORY_INFO
                    assert(panel.memory_queries == 0);
#else
                    assert(panel.memory_queries == 1); /* Same fact drives planning and adapter. */
#endif
                    assert(panel.calls == 0);
                    OK(present_resources_destroy(resources, 1000));
                    free_panel(&panel);
                }
            }
        }
    }
}

int main(void)
{
    region_direct_facts();
    check_size(8); /* Both direct and bounded dual-stage transport accept a tiny frame. */
    check_size(480);
    return 0;
}
