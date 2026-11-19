#include "platform/lcd/present_lcd_io.h"
/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "core/present_engine.h"
#include "resources/present_memory.h"
#include "support/legacy_panel.h"
#include "config/present_defaults.h"
/* Fail a real pixel-pool allocation, not the planning function. */
static unsigned fail_pixel_at, pixel_calls;
static bool fail_combined_fb_tiles, count_pixel_calls;
static size_t largest_internal_dma_block = SIZE_MAX;
bool __wrap_present_internal_dma_pool_available(size_t bytes)
{
    return bytes <= largest_internal_dma_block;
}

void *__real_present_alloc_pixels_in(size_t bytes, bool external);
void *__wrap_present_alloc_pixels_in(size_t bytes, bool external)
{
    if (fail_combined_fb_tiles && !external && bytes == 2 * 64) {
        return NULL;
    }
    if ((count_pixel_calls || fail_pixel_at) && ++pixel_calls == fail_pixel_at) {
        return NULL;
    }
    return __real_present_alloc_pixels_in(bytes, external);
}
static void optional_tile_fallback(void)
{
    for (unsigned kind = 0; kind < 4; ++kind) {
        for (unsigned explicit_count = 0; explicit_count < 2; ++explicit_count) {
            struct fake_lcd panel;
            init_panel(&panel);
            esp_display_presenter_config_t input = config(&panel, false);
            if (kind != 3) {
                input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
                input.target.hw.io = &panel;
                input.target.fb.mode = kind == 2 ? ESP_DISPLAY_PRESENT_MODE_TE_SYNC : ESP_DISPLAY_PRESENT_MODE_NONE;
            }
            if (kind == 1) {
                input.target.drawbuf.in_psram = true;
            }
            if (kind == 2) {
                panel.psram_direct = true; /* TE compose images use PSRAM; no factory staging. */
                input.target.hw.te_enabled = true;
                input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
                    .gpio_num = 1, .bus_freq_hz = 8000000
                };
            }
            input.target.drawbuf.buffers = explicit_count ? 2 : 0;
            /* GRAM: fail the second draw slot. TE: compose before two tiles.
             * FB borrows images; only tiles allocate here. */
            fail_pixel_at = kind == 2 ? 3 : 2;
            fail_combined_fb_tiles = kind == 3;
            pixel_calls = 0;
            present_resources_t *resources = NULL;
            esp_err_t error = present_resources_create(&input, false, NULL, NULL, &resources);
            fail_pixel_at = 0;
            fail_combined_fb_tiles = false;
            if (explicit_count) {
                assert(error == ESP_ERR_NO_MEM && !resources);
            } else {
                assert(error == ESP_OK);
                present_resource_status_t status;
                OK(present_resources_status(resources, &status));
                /* The reported plan must be the shrunk final attempt. */
                assert(status.plan.tiles.count == 1);
                /* Retry must update policy as well as buffer counts. In
                 * particular FB's automatic pair now shares its copy worker. */
                assert(status.plan.normalized.backend.copy_on_backend_worker == (kind >= 2));
                assert(!status.plan.normalized.backend.tx_staging);
                assert(status.plan.normalized.backend.async_copy == (kind >= 2));
                if (kind < 2) {
                    assert(!status.plan.normalized.background_service && !status.plan.normalized.backend.deferred_submit);
                }
                size_t stack_bytes = kind >= 2 ? 4096u : 0;
                assert(status.plan.stack_bytes == stack_bytes);
                if (kind != 3) {
                    assert(status.plan.normalized.backend.buffer_count == 1);
                }
                void *buffers[3];
                size_t count, bytes;
                OK(esp_display_present_engine_get_draw_buffers(present_resources_presenter(resources), buffers, 3, &count, &bytes));
                assert(count == 1 && bytes == 64 && panel.calls == 0);
                OK(present_resources_destroy(resources, 1000));
            }
            free_panel(&panel);
        }
    }
}
static void initialize(present_resources_t *resources, struct fake_lcd *panel)
{
    unsigned before = panel->calls;
    assert(present_resources_prepare(resources, 0) == ESP_ERR_TIMEOUT);
    assert(panel->calls == before + 1 && panel->source == panel->buffers[0]);
    boundary(panel);
    present_resource_status_t state;
    OK(present_resources_status(resources, &state));
    /* Successful post-submit ISR join makes one boundary sufficient for
     * both single-buffer exposure and multi-buffer retirement. */
    OK(present_resources_prepare(resources, 0));
    assert(panel->calls == before + 1); /* Retry never submits a second bootstrap. */
}
static void *submit_frame(present_resources_t *resources, struct fake_lcd *panel, bool hardware_error)
{
    esp_display_present_engine_t *presenter = present_resources_presenter(resources);
    esp_display_present_frame_request_t request = {.full_redraw = true};
    OK(esp_display_present_engine_begin_next_frame(presenter, &request));
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0x5a, 64);
    esp_display_present_engine_area_t area = {0, 0, 7, 3};
    unsigned before = panel->calls;
    OK(esp_display_present_engine_submit_buffer(presenter, &lease, &area, 16));
    esp_display_present_engine_receipt_t receipt;
    OK(esp_display_present_engine_commit_with_receipt(presenter, NULL, &receipt));
    esp_display_present_frame_status_t status;
    if (hardware_error) {
        pthread_mutex_lock(&executor_gate);
        executor_hold = false;
        pthread_cond_broadcast(&executor_changed);
        pthread_mutex_unlock(&executor_gate);
        OK(esp_display_present_engine_wait_frame(presenter, receipt, ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED, 1000, &status));
        assert(status.result == ESP_FAIL && !status.present_confirmed && panel->calls == before);
    } else {
        pthread_mutex_lock(&panel->lock);
        while (panel->calls == before) {
            pthread_cond_wait(&panel->changed, &panel->lock);
        }
        pthread_mutex_unlock(&panel->lock);
        OK(esp_display_present_engine_service(presenter)); /* Sender released gate: submit is armed. */
        boundary(panel);
        boundary(panel);
        OK(esp_display_present_engine_wait_frame(presenter, receipt, ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED, 1000, &status));
        assert(status.result == ESP_OK && ((const uint16_t *)panel->source)[0] == 0x5a5a);
    }
    OK(esp_display_present_engine_release_receipt(presenter, receipt));
    return lease.surface.pixels;
}

static void fb_reopen_two_internal_tiles(void)
{
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, true);
    input.target.drawbuf.buffers = 2;
    for (unsigned open = 0; open < 3; ++open) {
        input.target.fb.mode = open ? ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL : ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL;
        /* The warm reopen would fail on a second individual allocation.
         * Also exercise a heap that can fit two tiles in separate blocks. */
        fail_pixel_at = open == 1 ? 2 : 0;
        largest_internal_dma_block = open == 2 ? 64 : SIZE_MAX;
        count_pixel_calls = true;
        pixel_calls = 0;
        present_resources_t *resources;
        OK(present_resources_create(&input, true, NULL, NULL, &resources));
        assert(pixel_calls == (open == 2 ? 2 : 1));
        fail_pixel_at = 0;
        largest_internal_dma_block = SIZE_MAX;
        count_pixel_calls = false;

        initialize(resources, &panel);
        present_resource_status_t status;
        OK(present_resources_status(resources, &status));
        assert(status.plan.tiles.count == 2 && status.plan.tiles.memory == ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA);
        assert(status.plan.images.count == (open ? 3 : 2));
        void *tiles[2];
        size_t count, bytes;
        OK(esp_display_present_engine_get_draw_buffers(present_resources_presenter(resources), tiles, 2, &count, &bytes));
        assert(count == 2 && bytes == 64 && tiles[0] != tiles[1]);
        assert((uintptr_t)tiles[0] % 64 == 0 && (uintptr_t)tiles[1] % 64 == 0);
        if (open != 2) {
            assert((uintptr_t)tiles[1] - (uintptr_t)tiles[0] == bytes);
        }
        assert(submit_frame(resources, &panel, false) == tiles[0]);
        assert(submit_frame(resources, &panel, false) == tiles[1]);
        OK(present_resources_destroy(resources, 1000));
    }
    free_panel(&panel);
}

static void exercise(bool rgb, bool hardware)
{
    struct fake_lcd panel;
    init_panel(&panel);
    unavailable = !hardware;
    executor_failure = 0;
    esp_display_presenter_config_t input = config(&panel, rgb);
    present_resources_t *resources;
    OK(present_resources_create(&input, rgb, NULL, NULL, &resources));
    assert(panel.calls == 0 && !panel.deleted); /* Creation never sends a bootstrap. */
    initialize(resources, &panel);
    submit_frame(resources, &panel, false);
    present_resource_status_t status;
    OK(present_resources_status(resources, &status));
    assert(status.plan.images.count == 2 && status.plan.tiles.count == 2 && status.ready);
    assert(hardware ? status.hardware_copies == 1 : status.cpu_copies == 1);
    panel.detach_fail = true;
    assert(present_resources_destroy(resources, 1000) == ESP_FAIL);
    assert(!panel.deleted);
    panel.detach_fail = false;
    if (hardware) {
        destroy_failure = 1;
        assert(present_resources_destroy(resources, 1000) == ESP_FAIL);
        assert(!present_resources_presenter(resources));
    }
    OK(present_resources_destroy(resources, 1000));
    /* The old driver still scans its own FB. New owner explicitly establishes
     * FB0 before the core may lease what it believes to be the other buffer. */
    assert(((const uint16_t *)panel.source)[0] == 0x5a5a);
    OK(present_resources_create(&input, rgb, NULL, NULL, &resources));
    initialize(resources, &panel);
    submit_frame(resources, &panel, false);
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
}
static void io_path(void)
{
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, false);
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.hw.io = &panel;
    input.target.hw.swap_bytes = true;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    input.target.drawbuf.buffers = 1; /* D1 submits inline. */
    present_resources_t *resources;
    OK(present_resources_create(&input, false, NULL, NULL, &resources));
    OK(present_resources_prepare(resources, 0));
    assert(panel.calls == 0);
    esp_display_present_engine_t *presenter = present_resources_presenter(resources);
    esp_display_present_frame_request_t request = {.full_redraw = true};
    OK(esp_display_present_engine_begin_next_frame(presenter, &request));
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(presenter, &lease));
    for (unsigned i = 0; i < 32; ++i) {
        ((uint16_t *)lease.surface.pixels)[i] = 0x1234;
    }
    esp_display_present_engine_area_t area = {0, 0, 7, 3};
    OK(esp_display_present_engine_submit_buffer(presenter, &lease, &area, 16));
    assert(((const uint16_t *)panel.source)[0] == 0x3412);
    esp_display_present_engine_receipt_t receipt;
    OK(esp_display_present_engine_commit_with_receipt(presenter, NULL, &receipt));
    esp_display_present_frame_status_t status;
    assert(esp_display_present_engine_wait_frame(presenter, receipt, ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(present_resources_destroy(resources, 0) == ESP_ERR_TIMEOUT);
    (void)panel.io(&panel, NULL, panel.user);
    OK(esp_display_present_engine_query_frame(presenter, receipt, &status));
    assert(status.src_released && status.terminal);
    OK(esp_display_present_engine_release_receipt(presenter, receipt));
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
}
static void platform_matrix(void)
{
    const esp_display_present_mode_t modes[] = {
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT
    };
    for (unsigned rgb = 0; rgb < 2; ++rgb) {
        for (unsigned mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
            for (unsigned swap = 0; swap < 2; ++swap) {
                struct fake_lcd panel;
                init_panel(&panel);
                panel.physical_width = 6;
                panel.physical_height = 4;
                esp_display_presenter_config_t input = config(&panel, rgb);
                input.width = 6; /* 48 valid bytes, no invented tail capacity. */
                input.target.fb.mode = modes[mode];
                input.target.hw.swap_bytes = swap;
                unavailable = true;
                present_resources_t *resources;
                OK(present_resources_create(&input, rgb, NULL, NULL, &resources));
                if (modes[mode] == ESP_DISPLAY_PRESENT_MODE_NONE) {
                    OK(present_resources_prepare(resources, 0));
                    assert(panel.calls == 0);
                } else {
                    initialize(resources, &panel);
                }
                esp_display_present_engine_t *presenter = present_resources_presenter(resources);
                esp_display_present_frame_request_t request = {.full_redraw = true};
                OK(esp_display_present_engine_begin_next_frame(presenter, &request));
                esp_display_present_lease_t lease;
                OK(esp_display_present_engine_acquire_buffer(presenter, &lease));
                for (unsigned i = 0; i < 24; ++i) {
                    ((uint16_t *)lease.surface.pixels)[i] = 0x1200 + i;
                }
                unsigned before = panel.calls;
                esp_display_present_engine_area_t area = {0, 0, 5, 3};
                OK(esp_display_present_engine_submit_buffer(presenter, &lease, &area, 12));
                esp_display_present_engine_receipt_t receipt;
                OK(esp_display_present_engine_commit_with_receipt(presenter, NULL, &receipt));
                pthread_mutex_lock(&panel.lock);
                while (panel.calls == before) {
                    pthread_cond_wait(&panel.changed, &panel.lock);
                }
                pthread_mutex_unlock(&panel.lock);
                OK(esp_display_present_engine_service(presenter));
                boundary(&panel);
                boundary(&panel);
                esp_display_present_frame_status_t status;
                bool partition_none = modes[mode] == ESP_DISPLAY_PRESENT_MODE_NONE;
                OK(esp_display_present_engine_wait_frame(presenter, receipt,
                                                         partition_none ? ESP_DISPLAY_PRESENT_COMPLETION_SOURCE_RELEASED : ESP_DISPLAY_PRESENT_COMPLETION_PRESENT_CONFIRMED,
                                                         1000, &status));
                for (unsigned i = 0; i < 24; ++i) {
                    uint16_t expected = 0x1200 + i;
                    if (swap) {
                        expected = (uint16_t)((expected << 8) | (expected >> 8));
                    }
                    assert(((const uint16_t *)panel.source)[i] == expected);
                }
                if (!partition_none) {
                    for (unsigned i = 48; i < 64; ++i) {
                        assert(((const uint8_t *)panel.source)[i] == 0);
                    }
                }
                OK(esp_display_present_engine_release_receipt(presenter, receipt));
                OK(present_resources_destroy(resources, 1000));
                free_panel(&panel);
            }
        }
    }
}
static void scanout_policy(void)
{
    for (unsigned rgb = 0; rgb < 2; ++rgb) {
        for (unsigned count = 1; count <= 3; ++count) {
            struct fake_lcd panel;
            init_panel(&panel);
            panel.available_fbs = count;
            esp_display_presenter_config_t input = config(&panel, rgb);
            present_resources_t *resources = NULL;
            input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
            assert(present_resources_create(&input, false, NULL, NULL, &resources) == ESP_ERR_NOT_SUPPORTED);
            assert(!resources && !panel.calls && !panel.user);
            input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_AUTO;
            esp_err_t error = present_resources_create(&input, true, NULL, NULL, &resources);
            if (count == 1) {
                assert(error == ESP_ERR_NOT_SUPPORTED && !resources && !panel.user);
            } else {
                assert(error == ESP_OK);
                present_resource_status_t status;
                OK(present_resources_status(resources, &status));
                assert(status.plan.images.count == count);
                assert(status.plan.normalized.backend.render_contract == ESP_DISPLAY_PRESENT_ENGINE_CONTRACT_PARTITION);
                OK(present_resources_destroy(resources, 1000));
            }
            free_panel(&panel);
        }
    }
}
static void plans(void)
{
    esp_display_presenter_config_t io = config(NULL, false);
    io.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    io.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_AUTO;
    io.target.hw.te_enabled = true;
    io.target.hw.te_sync.gpio_num = -1;
    esp_display_present_plan_config_t io_plan;
    OK(present_resources_plan(&io, false, &io_plan));
    assert(io_plan.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_REGION && io_plan.presenter.mode == ESP_DISPLAY_PRESENT_ENGINE_MODE_NONE);
    io.target.hw.te_sync.gpio_num = 0; /* GPIO0 is valid: AUTO really requests TE. */
    OK(present_resources_plan(&io, false, &io_plan));
    assert(io_plan.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_TE && io_plan.resources.image_count == 1);
    io.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    OK(present_resources_plan(&io, false, &io_plan)); /* Explicit NONE, not silent TE fallback. */

    esp_display_presenter_config_t input = config(NULL, false);
    esp_display_present_plan_config_t plan;
    const esp_display_present_mode_t modes[] = {
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL,
        ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_AUTO,
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        input.target.fb.mode = modes[i];
        OK(present_resources_plan(&input, false, &plan));
        bool triple = i == 1 || i == 4 || i == 5;
        bool partial = i >= 3;
        assert(plan.resources.image_count == (triple ? 3u : 2u));
        assert(plan.resources.tile_count == (partial ? 2u : 0u));
        assert(plan.presenter.background_service && plan.presenter.backend.async_copy);
    }
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    assert(present_resources_plan(&input, false, &plan) == ESP_ERR_NOT_SUPPORTED);
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL;
    input.width = 4;
    OK(present_resources_plan(&input, false, &plan));
}

/* Full factory admission matrix. This pins present_resources_plan only:
 * success/error, backend kind, resolved mode, image and tile counts for
 * every public mode at every rotation. It does not create hardware-facing
 * resources, so scanout buffer counts come from the mode, not the panel. */
static void factory_mode_matrix(void)
{
    static const esp_display_present_mode_t fb_modes[] = {
        ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL,
        ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL,
    };
    static const size_t fb_images[] = {0, 2, 3, 2, 2, 3};
    for (unsigned panel_type = 0; panel_type < 2; ++panel_type) {
        for (unsigned m = 0; m < sizeof(fb_modes) / sizeof(fb_modes[0]); ++m) {
            for (unsigned rotation = 0; rotation < 4; ++rotation) {
                esp_display_presenter_config_t input = config(NULL, panel_type == 0);
                input.target.hw.panel_type = panel_type == 0 ? ESP_DISPLAY_PRESENT_PANEL_RGB : ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI;
                input.target.fb.mode = fb_modes[m];
                input.target.hw.rotation = rotation * 90;
                esp_display_present_plan_config_t plan;
                esp_err_t error = present_resources_plan(&input, false, &plan);
                if (fb_modes[m] == ESP_DISPLAY_PRESENT_MODE_NONE) {
                    assert(error == ESP_ERR_NOT_SUPPORTED);
                    continue;
                }
                assert(error == ESP_OK);
                assert(plan.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_FB);
                assert(plan.resources.image_count == fb_images[m]);
                bool partial = fb_modes[m] == ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL ||
                               fb_modes[m] == ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL;
                assert(plan.resources.tile_count == (partial ? 2u : 0u));
                assert(plan.presenter.backend.rotation == (esp_display_present_engine_rotation_t)rotation);
                assert(plan.presenter.backend.async_copy && plan.presenter.background_service);
            }
        }
    }
    static const esp_display_present_mode_t io_modes[] = {
        ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_MODE_TE_SYNC,
    };
    for (unsigned m = 0; m < sizeof(io_modes) / sizeof(io_modes[0]); ++m) {
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            esp_display_presenter_config_t input = config(NULL, false);
            input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
            input.target.fb.mode = io_modes[m];
            input.target.hw.rotation = rotation * 90;
            input.target.hw.te_enabled = true;
            input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
                .gpio_num = 1, .bus_freq_hz = 8000000
            };
            esp_display_present_plan_config_t plan;
            esp_err_t error = present_resources_plan(&input, false, &plan);
            assert(error == ESP_OK);
            if (io_modes[m] == ESP_DISPLAY_PRESENT_MODE_TE_SYNC) {
                assert(plan.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_TE);
                assert(plan.resources.image_count == 1);
            } else {
                /* Explicit NONE stays REGION even with a valid TE input. */
                assert(plan.presenter.backend_kind == ESP_DISPLAY_PRESENT_ENGINE_REGION);
                assert(plan.resources.image_count == 0);
            }
            assert(plan.presenter.backend.rotation == (esp_display_present_engine_rotation_t)rotation);
        }
    }
}

/* The facade's automatic TE context is a separate framework allocation and
 * must flow through the resolved total-budget check. */
static void automatic_te_memory_budget(void)
{
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, false);
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.hw.io = &panel;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    input.target.hw.te_enabled = true;
    input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
        .gpio_num = 1, .bus_freq_hz = 8000000
    };
    esp_display_present_plan_config_t plan;
    esp_display_present_resolved_plan_t before, after;
    te_memory_test_extra = 0;
    OK(present_resources_plan(&input, false, &plan));
    size_t framework_bytes = plan.extra_bytes;
    uint32_t width = input.width, height = input.height;
    input.width = input.height = 480;
    OK(present_resources_plan(&input, false, &plan));
    assert(plan.extra_bytes - framework_bytes ==
           esp_display_present_lcd_io_memory(16320) - esp_display_present_lcd_io_memory(64));
    assert(esp_display_present_lcd_io_memory(16320) - esp_display_present_lcd_io_memory(0) < 33 * 1024);
    input.width = width;
    input.height = height;
    OK(present_resources_plan(&input, false, &plan));
    /* Normally filled by the created SPI adapter before resolve. */
    plan.output = (esp_display_present_output_caps_t) {
        .internal_dma = true, .ext_mem = true, .tight_stride = true
    };
    OK(esp_display_present_plan_resolve(&plan, &before));
    te_memory_test_extra = 123;
    OK(present_resources_plan(&input, false, &plan));
    plan.output = (esp_display_present_output_caps_t) {
        .internal_dma = true, .ext_mem = true, .tight_stride = true
    };
    OK(esp_display_present_plan_resolve(&plan, &after));
    assert(after.reserved_bytes == before.reserved_bytes + 123);
    assert(after.required_bytes == before.required_bytes + 123);
    plan.total_budget_bytes = after.required_bytes - 1;
    assert(esp_display_present_plan_resolve(&plan, &after) == ESP_ERR_NO_MEM);
    te_memory_test_extra = 0;
    free_panel(&panel);
}

static void tile_count_and_te_workers(void)
{
    for (unsigned count = 0; count <= 2; ++count) {
        esp_display_presenter_config_t input = config(NULL, false);
        input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
        input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
        input.target.drawbuf.buffers = count;
        esp_display_present_plan_config_t plan;
        OK(present_resources_plan(&input, false, &plan));
        assert(!plan.presenter.backend.async_copy);
        bool expected = count != 1;
        assert(plan.presenter.backend.deferred_submit == expected);
        assert(plan.presenter.background_service == expected);
        assert(plan.presenter.worker_priority == (expected ? CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY : 0));
        input.target.hw.rotation = 90;
        assert(present_resources_plan(&input, false, &plan) ==
               ESP_OK);
    }
    esp_display_presenter_config_t input = config(NULL, false);
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.hw.te_enabled = true;
    input.target.hw.te_sync = (esp_display_present_te_sync_config_t) {
        .gpio_num = 1, .bus_freq_hz = 8000000
    };
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    for (unsigned images = 1; images <= 2; ++images) {
        for (unsigned tiles = 1; tiles <= 2; ++tiles) {
            input.target.drawbuf.te_compose_buffers = images;
            input.target.drawbuf.buffers = tiles;
            esp_display_present_plan_config_t plan;
            OK(present_resources_plan(&input, false, &plan));
            assert(plan.presenter.backend.async_copy && plan.presenter.background_service);
            assert(plan.presenter.backend.copy_on_backend_worker == (images == 1));
            assert(plan.presenter.worker_priority == CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY);
            plan.output = (esp_display_present_output_caps_t) {
                .internal_dma = true, .ext_mem = true, .tight_stride = true
            };
            esp_display_present_resolved_plan_t resolved;
            OK(esp_display_present_plan_resolve(&plan, &resolved));
            assert(resolved.stack_bytes == (images == 1 ? 4096 : 8192));
            if (images == 1) {
                size_t shared_bytes = resolved.required_bytes;
                plan.presenter.backend.copy_on_backend_worker = false;
                OK(esp_display_present_plan_resolve(&plan, &resolved));
                assert(resolved.required_bytes == shared_bytes + 4096 + present_worker_memory());
            }
        }
    }
}

int main(void)
{
    tile_count_and_te_workers();
    automatic_te_memory_budget();
    optional_tile_fallback();
    fb_reopen_two_internal_tiles();
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, true);
    esp_display_present_plan_config_t plan;
    OK(present_resources_plan(&input, false, &plan)); /* Actual RGB facts are queried at create. */
    input.target.hw.te_enabled = true;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    assert(present_resources_plan(&input, true, &plan) == ESP_ERR_NOT_SUPPORTED);
    input.target.hw.te_enabled = false;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL;
    panel.attach_fail = true;
    present_resources_t *resources = NULL;
    esp_err_t create_error = present_resources_create(&input, true, NULL, NULL, &resources);
    assert(create_error == ESP_FAIL && !resources);
    panel.attach_fail = false;
    free_panel(&panel);
    plans();
    factory_mode_matrix();
    scanout_policy();
    platform_matrix();
    io_path();
    exercise(false, false);
    exercise(true, false);
    exercise(false, true);
    init_panel(&panel);
    input = config(&panel, false);
    unavailable = false;
    executor_failure = 1;
    executor_hold = true; /* Accepted frame before asynchronous execution failure. */
    OK(present_resources_create(&input, false, NULL, NULL, &resources));
    initialize(resources, &panel);
    unsigned before = executor_calls;
    submit_frame(resources, &panel, true);
    present_resource_status_t status;
    OK(present_resources_status(resources, &status));
    assert(executor_calls == before + 1 && status.cpu_copies == 0); /* Never CPU retry an execution failure. */
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
    puts("resources: defaults, bootstrap, retained scanout, attach/stop/destroy retry and executor policy passed");
    return 0;
}
