/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "support/legacy_panel.h"

static bool psram_available = true;
bool __wrap_present_psram_can_fit(size_t bytes, size_t count)
{
    (void)bytes;
    (void)count;
    return psram_available;
}

enum { STAGED = 1, ASYNC = 2, IN_PLACE = 4 };

/* Baseline truth table, including preference surviving an internal fallback.
 * Entries are 0/90/180/270 degrees. Do not derive these from production rules. */
static const struct {
    bool prefer_psram, available, swap;
    unsigned flags[4];
} pixel_cases[] = {
    {false, true,  false, {0, ASYNC, ASYNC, ASYNC}},
    {false, true,  true,  {IN_PLACE, ASYNC, ASYNC, ASYNC}},
    {true,  true,  false, {STAGED | ASYNC, STAGED | ASYNC, STAGED | ASYNC, STAGED | ASYNC}},
    {true,  true,  true,  {STAGED | ASYNC, STAGED | ASYNC, STAGED | ASYNC, STAGED | ASYNC}},
    {true,  false, false, {0, ASYNC, ASYNC, ASYNC}},
    {true,  false, true,  {ASYNC, ASYNC, ASYNC, ASYNC}},
};

static void region_policy(void)
{
    const bool background[] = {
#ifdef CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND
        CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND,
#else
        false,
#endif
        false, true,
    };
    for (size_t c = 0; c < sizeof(pixel_cases) / sizeof(pixel_cases[0]); ++c) {
        psram_available = pixel_cases[c].available;
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            for (unsigned count = 0; count <= 2; ++count) {
                for (unsigned submit = 0; submit < 3; ++submit) {
                    /* These 16-row tiles cannot satisfy quarter-turn direct
                     * DMA geometry, so both IO capabilities retain staging. */
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
                        input.target.io_submit = (esp_display_present_io_submit_t)submit;
                        unsigned flags = pixel_cases[c].flags[rotation];
                        bool asynchronous = (flags & ASYNC) != 0;
                        esp_display_present_plan_config_t plan;
                        esp_err_t error = present_resources_plan(&input, false, &plan);
                        assert(error == (asynchronous && submit ? ESP_ERR_INVALID_ARG : ESP_OK));
                        if (error != ESP_OK) {
                            continue;
                        }
                        assert(plan.presenter.backend.tx_staging == ((flags & STAGED) != 0));
                        assert(plan.presenter.backend.async_copy == asynchronous);
                        assert(plan.presenter.backend.in_place == ((flags & IN_PLACE) != 0));
                        assert(!plan.presenter.backend.shared_copy_worker);
                        bool deferred = !asynchronous && background[submit];
                        assert(plan.presenter.backend.deferred_submit == deferred);
                        assert(plan.presenter.background_service == (asynchronous || deferred));
#ifdef CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY
                        const unsigned priority = CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY;
#else
                        const unsigned priority = 7;
#endif
                        assert(plan.presenter.worker_priority == (deferred ? priority : 0));
                        size_t tiles = count ? count : 2;
                        assert(plan.resources.tile_count == tiles);
                        plan.output = (esp_display_present_output_caps_t) {
                            .internal_dma = true, .ext_mem = true, .tight_stride = true,
                        };
                        esp_display_present_resolved_plan_t resolved;
                        OK(esp_display_present_plan_resolve(&plan, &resolved));
                        assert(resolved.tiles.bytes_each == 15360);
                        assert(resolved.tiles.memory == (pixel_cases[c].prefer_psram && psram_available ?
                                                         ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL : ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA));
                        assert(resolved.conversion.count == (asynchronous ? tiles : 0));
                        assert(resolved.conversion.memory == ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA);
                        assert(resolved.pixel_bytes == tiles * 15360 * (asynchronous ? 2 : 1));
                        assert(resolved.stack_bytes == (asynchronous ? 8192u : deferred ? 4096u : 0));
                    }
                }
            }
        }
    }
    psram_available = true;
}

static void direct_submission_policy(void)
{
    psram_available = true;
    const bool background[] = {
#ifdef CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND
        CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND,
#else
        false,
#endif
        false, true,
    };
    for (unsigned submit = 0; submit < 3; ++submit) {
        esp_display_presenter_config_t input = config(NULL, false);
        input.width = input.height = 64;
        input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
        input.target.hw.spi_psram_dma_direct = true;
        input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
        input.target.drawbuf.in_psram = true;
        input.target.drawbuf.lines = 32;
        input.target.drawbuf.buffers = 1;
        input.target.io_submit = (esp_display_present_io_submit_t)submit;
        esp_display_present_plan_config_t plan;
        OK(present_resources_plan(&input, false, &plan));
        assert(!plan.presenter.backend.tx_staging && !plan.presenter.backend.async_copy);
        assert(!plan.presenter.backend.in_place && !plan.presenter.backend.shared_copy_worker);
        assert(plan.presenter.backend.deferred_submit == background[submit]);
        assert(plan.presenter.background_service == background[submit]);
        plan.output = (esp_display_present_output_caps_t) {
            .internal_dma = true, .ext_mem = true, .tight_stride = true,
            .ext_len_align = 64,
        };
        esp_display_present_resolved_plan_t resolved;
        OK(esp_display_present_plan_resolve(&plan, &resolved));
        assert(resolved.conversion.count == 1 && resolved.conversion.memory == ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL);
        assert(resolved.pixel_bytes == 2 * 64 * 32 * 2); /* Peak, not initial allocation. */
        assert(resolved.stack_bytes == (background[submit] ? 4096u : 0));
        size_t peak = resolved.pixel_bytes;
        plan.pixel_budget_bytes = peak - 1;
        assert(esp_display_present_plan_resolve(&plan, &resolved) == ESP_ERR_NO_MEM);
        plan.pixel_budget_bytes = peak;
        OK(esp_display_present_plan_resolve(&plan, &resolved));
        assert(resolved.conversion.count == 1 && resolved.pixel_bytes == peak);
    }
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
                        assert(plan.presenter.backend.shared_copy_worker == (te ? images == 1 : tiles <= 1));
                        assert(plan.presenter.backend.async_copy && plan.presenter.background_service);
                        assert(!plan.presenter.backend.tx_staging && !plan.presenter.backend.in_place);
                        assert(!plan.presenter.backend.deferred_submit);
                        assert(plan.presenter.worker_priority == (te && images == 2 ? 7 : 0));
                    }
                }
            }
        }
    }
}

int main(void)
{
    region_policy();
    direct_submission_policy();
    framebuffer_and_te_workers();
    return 0;
}
