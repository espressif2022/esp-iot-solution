/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "support/legacy_panel.h"
#include "config/present_defaults.h"
#include "platform/lcd/present_lcd_io.h"
int main(void)
{
#ifdef TEST_SMALL_BUDGETS
    const size_t rows = 8, stage = 3840;
#else
    const size_t rows = 34, stage = 16320;
#endif
    assert(present_default_tile_rows(960, 480) == rows);
    assert(present_default_tile_rows(960, 4) == 4);
    assert(present_default_tile_rows(2 * 1024 * 1024, 2) == 1);
    assert(present_default_te_stage_bytes(960, 480) == stage);
    assert(present_default_te_stage_bytes(2 * 1024 * 1024, 2) == 0);
    (void)boundary; /* Planning only; no fake scanout events. */
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, false);
    input.target.hw.io = &panel;
    input.width = input.height = 480;
    input.target.drawbuf.lines = 0;
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.hw.te_enabled = true;
    input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
        .gpio_num = 1, .bus_freq_hz = 8000000
    };
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    esp_display_present_plan_config_t plan;
    OK(present_resources_plan(&input, false, &plan));
    assert(plan.presenter.backend.tile_rows == rows);
    size_t staged_bytes = plan.extra_bytes;
    input.target.hw.spi_psram_dma_direct = true;
    panel.psram_direct = true;
    OK(present_resources_plan(&input, false, &plan));
    assert(staged_bytes - plan.extra_bytes ==
           esp_display_present_lcd_io_memory(stage) - esp_display_present_lcd_io_memory(0));
    plan.output = (esp_display_present_output_caps_t) {
        .internal_dma = true, .ext_mem = true, .tight_stride = true
    };
    plan.presenter.backend.tile_rows = 0; /* Exercise the lower-level plan default too. */
    esp_display_present_resolved_plan_t resolved;
    OK(esp_display_present_plan_resolve(&plan, &resolved));
    assert(resolved.normalized.backend.tile_rows == rows);
    input.target.drawbuf.lines = 5;
    OK(present_resources_plan(&input, false, &plan));
    assert(plan.presenter.backend.tile_rows == 5);
    free_panel(&panel);
    return 0;
}
