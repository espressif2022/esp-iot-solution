/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "support/legacy_panel.h"
static void initialize(pv2_legacy_resources_t *resources, struct fake_lcd *panel)
{
    unsigned before = panel->calls;
    assert(pv2_legacy_resources_prepare(resources, 0) == ESP_ERR_TIMEOUT);
    assert(panel->calls == before + 1 && panel->source == panel->buffers[0]);
    boundary(panel);
    assert(pv2_legacy_resources_prepare(resources, 0) == ESP_ERR_TIMEOUT);
    boundary(panel);
    OK(pv2_legacy_resources_prepare(resources, 0));
    assert(panel->calls == before + 1); /* Retry never submits a second bootstrap. */
}
static void submit_frame(pv2_legacy_resources_t *resources, struct fake_lcd *panel, bool hardware_error)
{
    pv2_presenter_t *presenter = pv2_legacy_resources_presenter(resources);
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    memset(lease.surface.pixels, 0x5a, 64);
    pv2_area_t area = {0, 0, 7, 3};
    unsigned before = panel->calls;
    OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 16));
    pv2_presenter_receipt_t receipt;
    OK(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt));
    pv2_frame_status_t status;
    if (hardware_error) {
        pthread_mutex_lock(&executor_gate);
        executor_hold = false;
        pthread_cond_broadcast(&executor_changed);
        pthread_mutex_unlock(&executor_gate);
        OK(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_SOURCE_RELEASED, 1000, &status));
        assert(status.result == ESP_FAIL && !status.present_confirmed && panel->calls == before);
    } else {
        pthread_mutex_lock(&panel->lock);
        while (panel->calls == before) {
            pthread_cond_wait(&panel->changed, &panel->lock);
        }
        pthread_mutex_unlock(&panel->lock);
        OK(pv2_presenter_service(presenter)); /* Sender released gate: submit is armed. */
        boundary(panel);
        boundary(panel);
        OK(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 1000, &status));
        assert(status.result == ESP_OK && ((const uint16_t *)panel->source)[0] == 0x5a5a);
    }
    OK(pv2_presenter_release_receipt(presenter, receipt));
}
static void exercise(bool rgb, bool hardware)
{
    struct fake_lcd panel;
    init_panel(&panel);
    unavailable = !hardware;
    executor_failure = 0;
    esp_display_presenter_config_t input = config(&panel, rgb);
    pv2_legacy_resources_t *resources;
    OK(pv2_legacy_resources_create(&input, rgb, NULL, NULL, &resources));
    assert(panel.calls == 0 && !panel.deleted); /* Creation never sends a bootstrap. */
    initialize(resources, &panel);
    submit_frame(resources, &panel, false);
    pv2_legacy_resource_status_t status;
    OK(pv2_legacy_resources_status(resources, &status));
    assert(status.plan.images.count == 2 && status.plan.tiles.count == 2 && status.ready);
    assert(hardware ? status.hardware_copies == 1 : status.cpu_copies == 1);
    panel.detach_fail = true;
    assert(pv2_legacy_resources_destroy(resources, 1000) == ESP_FAIL);
    assert(!panel.deleted);
    panel.detach_fail = false;
    if (hardware) {
        destroy_failure = 1;
        assert(pv2_legacy_resources_destroy(resources, 1000) == ESP_FAIL);
        assert(!pv2_legacy_resources_presenter(resources));
    }
    OK(pv2_legacy_resources_destroy(resources, 1000));
    /* The old driver still scans its own FB. New owner explicitly establishes
     * FB0 before the core may lease what it believes to be the other buffer. */
    assert(((const uint16_t *)panel.source)[0] == 0x5a5a);
    OK(pv2_legacy_resources_create(&input, rgb, NULL, NULL, &resources));
    initialize(resources, &panel);
    submit_frame(resources, &panel, false);
    OK(pv2_legacy_resources_destroy(resources, 1000));
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
    pv2_legacy_resources_t *resources;
    OK(pv2_legacy_resources_create(&input, false, NULL, NULL, &resources));
    OK(pv2_legacy_resources_prepare(resources, 0));
    assert(panel.calls == 0);
    pv2_presenter_t *presenter = pv2_legacy_resources_presenter(resources);
    pv2_frame_request_t request = {.full_redraw = true};
    OK(pv2_presenter_begin_next_frame(presenter, &request));
    pv2_lease_t lease;
    OK(pv2_presenter_acquire_buffer(presenter, &lease));
    for (unsigned i = 0; i < 32; ++i) {
        ((uint16_t *)lease.surface.pixels)[i] = 0x1234;
    }
    pv2_area_t area = {0, 0, 7, 3};
    OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 16));
    assert(((const uint16_t *)panel.source)[0] == 0x3412);
    pv2_presenter_receipt_t receipt;
    OK(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt));
    pv2_frame_status_t status;
    assert(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_SOURCE_RELEASED, 0, &status) == ESP_ERR_TIMEOUT);
    assert(pv2_legacy_resources_destroy(resources, 0) == ESP_ERR_TIMEOUT);
    (void)panel.io(&panel, NULL, panel.user);
    OK(pv2_presenter_query_frame(presenter, receipt, &status));
    assert(status.source_released && status.terminal);
    OK(pv2_presenter_release_receipt(presenter, receipt));
    OK(pv2_legacy_resources_destroy(resources, 1000));
    free_panel(&panel);
}
static void platform_matrix(void)
{
    const esp_display_present_mode_t modes[] = {ESP_DISPLAY_PRESENT_MODE_NONE,
                                                ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL,
                                                ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT
                                               };
    for (unsigned rgb = 0; rgb < 2; ++rgb) {
        for (unsigned mode = 0; mode < 4; ++mode) {
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
                pv2_legacy_resources_t *resources;
                OK(pv2_legacy_resources_create(&input, rgb, NULL, NULL, &resources));
                initialize(resources, &panel);
                pv2_presenter_t *presenter = pv2_legacy_resources_presenter(resources);
                pv2_frame_request_t request = {.full_redraw = true};
                OK(pv2_presenter_begin_next_frame(presenter, &request));
                pv2_lease_t lease;
                OK(pv2_presenter_acquire_buffer(presenter, &lease));
                for (unsigned i = 0; i < 24; ++i) {
                    ((uint16_t *)lease.surface.pixels)[i] = 0x1200 + i;
                }
                unsigned before = panel.calls;
                pv2_area_t area = {0, 0, 5, 3};
                OK(pv2_presenter_submit_buffer(presenter, &lease, &area, 12));
                pv2_presenter_receipt_t receipt;
                OK(pv2_presenter_commit_with_receipt(presenter, NULL, &receipt));
                pthread_mutex_lock(&panel.lock);
                while (panel.calls == before) {
                    pthread_cond_wait(&panel.changed, &panel.lock);
                }
                pthread_mutex_unlock(&panel.lock);
                OK(pv2_presenter_service(presenter));
                boundary(&panel);
                boundary(&panel);
                pv2_frame_status_t status;
                OK(pv2_presenter_wait_frame(presenter, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 1000, &status));
                for (unsigned i = 0; i < 24; ++i) {
                    uint16_t expected = 0x1200 + i;
                    if (swap) {
                        expected = (uint16_t)((expected << 8) | (expected >> 8));
                    }
                    assert(((const uint16_t *)panel.source)[i] == expected);
                }
                for (unsigned i = 48; i < 64; ++i) {
                    assert(((const uint8_t *)panel.source)[i] == 0);
                }
                OK(pv2_presenter_release_receipt(presenter, receipt));
                OK(pv2_legacy_resources_destroy(resources, 1000));
                free_panel(&panel);
            }
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
    pv2_plan_config_t io_plan;
    OK(pv2_legacy_plan(&io, false, &io_plan));
    assert(io_plan.presenter.backend_kind == PV2_PRESENTER_GRAM && io_plan.presenter.mode == PV2_MODE_NONE);
    io.target.hw.te_sync.gpio_num = 0; /* GPIO0 is valid: AUTO really requests TE. */
    OK(pv2_legacy_plan(&io, false, &io_plan));
    assert(io_plan.presenter.backend_kind == PV2_PRESENTER_TE && io_plan.resources.image_count == 1);
    io.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    OK(pv2_legacy_plan(&io, false, &io_plan)); /* Explicit NONE, not silent TE fallback. */

    esp_display_presenter_config_t input = config(NULL, false);
    pv2_plan_config_t plan;
    const esp_display_present_mode_t modes[] = {
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL,
        ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL,
        ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_MODE_AUTO,
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        input.target.fb.mode = modes[i];
        OK(pv2_legacy_plan(&input, false, &plan));
        bool triple = i == 1 || i == 4 || i == 5;
        bool partial = i >= 3;
        assert(plan.resources.image_count == (triple ? 3u : 2u));
        assert(plan.resources.tile_count == (partial ? 2u : 0u));
        assert(plan.presenter.background_service && plan.presenter.backend.async_copy);
    }
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    OK(pv2_legacy_plan(&input, false, &plan));
    assert(plan.resources.image_count == 1 && plan.presenter.backend.allow_active_scanout_writes);
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL;
    input.width = 4;
    OK(pv2_legacy_plan(&input, false, &plan));
}

int main(void)
{
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t input = config(&panel, true);
    pv2_plan_config_t plan;
    OK(pv2_legacy_plan(&input, false, &plan)); /* Actual RGB facts are queried at create. */
    input.target.hw.te_enabled = true;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_TE_SYNC;
    assert(pv2_legacy_plan(&input, true, &plan) == ESP_ERR_NOT_SUPPORTED);
    input.target.hw.te_enabled = false;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL;
    panel.attach_fail = true;
    pv2_legacy_resources_t *resources = NULL;
    esp_err_t create_error = pv2_legacy_resources_create(&input, true, NULL, NULL, &resources);
    assert(create_error == ESP_FAIL && !resources);
    panel.attach_fail = false;
    free_panel(&panel);
    plans();
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
    OK(pv2_legacy_resources_create(&input, false, NULL, NULL, &resources));
    initialize(resources, &panel);
    unsigned before = executor_calls;
    submit_frame(resources, &panel, true);
    pv2_legacy_resource_status_t status;
    OK(pv2_legacy_resources_status(resources, &status));
    assert(executor_calls == before + 1 && status.cpu_copies == 0); /* Never CPU retry an execution failure. */
    OK(pv2_legacy_resources_destroy(resources, 1000));
    free_panel(&panel);
    puts("resources: defaults, bootstrap, retained scanout, attach/stop/destroy retry and executor policy passed");
    return 0;
}
