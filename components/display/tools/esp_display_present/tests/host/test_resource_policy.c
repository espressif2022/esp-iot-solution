/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "support/legacy_panel.h"
#include "config/present_defaults.h"

static bool psram_available = true;
bool __wrap_present_psram_can_fit(size_t bytes, size_t count)
{
    (void)bytes;
    (void)count;
    return psram_available;
}

/* Final input count alone selects the REGION submission task. Routing and
 * memory fallback must not change it. panel_io_emit checks wire ownership. */
static const struct {
    bool prefer_psram, available, swap;
} pixel_cases[] = {
    {false, true, false}, {false, true, true}, {true, true, false},
    {true, true, true}, {true, false, false}, {true, false, true},
};

static void region_policy(void)
{
    for (size_t c = 0; c < sizeof(pixel_cases) / sizeof(pixel_cases[0]); ++c) {
        psram_available = pixel_cases[c].available;
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            for (unsigned count = 0; count <= 2; ++count) {
                /* Direct capability changes routing, never task or D/X counts. */
                for (unsigned direct = 0; direct < 2; ++direct) {
                    esp_display_presenter_config_t input = config(NULL, false);
                    input.width = input.height = 480;
                    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
                    input.target.hw.rotation = rotation * 90;
                    input.target.hw.swap_bytes = pixel_cases[c].swap;
                    input.target.hw.spi_psram_dma_direct = direct;
                    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
                    input.target.drawbuf.lines = 16;
                    input.target.drawbuf.buffers = count;
                    input.target.drawbuf.in_psram = pixel_cases[c].prefer_psram;
                    esp_display_present_plan_config_t plan;
                    OK(present_resources_plan(&input, false, &plan));
                    assert(!plan.presenter.backend.tx_staging && !plan.presenter.backend.async_copy);
                    assert(!plan.presenter.backend.in_place && !plan.presenter.backend.copy_on_backend_worker);
                    size_t tiles = count ? count : 2;
                    bool deferred = tiles == 2;
                    assert(plan.resources.tile_count == tiles);
                    assert(plan.presenter.backend.deferred_submit == deferred);
                    assert(plan.presenter.background_service == deferred);
                    assert(plan.presenter.worker_priority == (deferred ? CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY : 0));
                    plan.output = (esp_display_present_output_caps_t) {
                        .internal_dma = true, .ext_mem = true, .tight_stride = true,
                    };
                    esp_display_present_resolved_plan_t resolved;
                    OK(esp_display_present_plan_resolve(&plan, &resolved));
                    assert(resolved.tiles.bytes_each == 15360);
                    assert(resolved.tiles.memory == (pixel_cases[c].prefer_psram && psram_available ?
                                                     ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL : ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA));
                    assert(!resolved.conversion.count && resolved.conversion.memory == ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA);
                    assert(resolved.pixel_bytes == tiles * 15360);
                    assert(resolved.stack_bytes == (deferred ? 4096u : 0));
                    plan.total_budget_bytes = resolved.required_bytes;
                    OK(esp_display_present_plan_resolve(&plan, &resolved));
                    --plan.total_budget_bytes;
                    assert(esp_display_present_plan_resolve(&plan, &resolved) == ESP_ERR_NO_MEM);
                }
            }
        }
    }
    psram_available = true;
}

static void full_screen_partition_budget(void)
{
    esp_display_presenter_config_t input = config(NULL, false);
    input.width = input.height = 480;
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    input.target.drawbuf = (esp_display_present_drawbuf_config_t) {
        .lines = 480, .buffers = 2, .in_psram = true
    };
    esp_display_present_plan_config_t plan;
    OK(present_resources_plan(&input, false, &plan));
    size_t reserved = plan.extra_bytes;
    assert(reserved >= 2 * 16320 && reserved < 40 * 1024);
    plan.output = (esp_display_present_output_caps_t) {
        .internal_dma = true, .ext_mem = true, .tight_stride = true
    };
    esp_display_present_resolved_plan_t resolved;
    OK(esp_display_present_plan_resolve(&plan, &resolved));
    assert(resolved.tiles.count == 2 && resolved.tiles.bytes_each == 460800);
    assert(resolved.tiles.memory == ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL && !resolved.conversion.count);
    assert(resolved.pixel_bytes == 921600 && resolved.stack_bytes == 4096);
    input.target.drawbuf.lines = 16;
    OK(present_resources_plan(&input, false, &plan));
    assert(plan.extra_bytes == reserved); /* Internal S budget does not grow with D. */
}

static void framebuffer_and_te_workers(void)
{
    static const esp_display_present_mode_t modes[] = {
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL,
        ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_TE_SYNC,
    };
    for (unsigned transport = 0; transport < 3; ++transport) {
        for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m) {
            bool te = modes[m] == ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
            if (te != (transport == 2)) {
                continue;
            }
            for (unsigned images = 1; images <= (te ? 2u : 1u); ++images) {
                for (unsigned count = 0; count <= 2; ++count) {
                    for (unsigned rotation = 0; rotation < 4; ++rotation) {
                        esp_display_presenter_config_t input = config(NULL, transport == 0);
                        input.target.hw.panel_type = transport == 2 ? ESP_DISPLAY_PRESENT_PANEL_IO :
                                                     transport == 0 ? ESP_DISPLAY_PRESENT_PANEL_RGB : ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI;
                        input.target.hw.rotation = rotation * 90;
                        input.target.fb.mode = modes[m];
                        input.target.drawbuf.buffers = count;
                        input.target.drawbuf.te_compose_buffers = te ? images : 0;
                        input.target.hw.te_enabled = te;
                        input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
                            .gpio_num = 1, .bus_freq_hz = 8000000,
                        };
                        esp_display_present_plan_config_t plan;
                        OK(present_resources_plan(&input, false, &plan));
                        bool partial = te || modes[m] == ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL ||
                                       modes[m] == ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL;
                        size_t tiles = partial ? count ? count : 2 : 0;
                        assert(plan.resources.tile_count == tiles);
                        assert(plan.presenter.backend.copy_on_backend_worker == (te ? images == 1 : tiles <= 1));
                        assert(plan.presenter.backend.async_copy && plan.presenter.background_service);
                        assert(!plan.presenter.backend.tx_staging && !plan.presenter.backend.in_place);
                        assert(!plan.presenter.backend.deferred_submit);
                        assert(plan.presenter.worker_priority == CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY);
                    }
                }
            }
        }
    }
}

int main(void)
{
    region_policy();
    full_screen_partition_budget();
    framebuffer_and_te_workers();
    return 0;
}
