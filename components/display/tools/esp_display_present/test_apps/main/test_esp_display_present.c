/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
/* Public API smoke. Lifecycle/pixel tests live in the V2 Host suites. */
#include <stdio.h>
#include "esp_display_present.h"
#include "esp_display_present_geometry.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "unity_test_utils_memory.h"
TEST_CASE("framebuffer requirements validate policy before panel creation",
          "[present][config]")
{
    const struct {
        esp_display_present_panel_t panel;
        esp_display_present_mode_t mode;
        esp_display_present_rotation_t rotation;
        bool te_enabled;
        int te_gpio;
        esp_err_t result;
        uint8_t count;
    } cases[] = {
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_OK, 1},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_ROTATE_90, false, -1, ESP_OK, 2},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_TRIPLE_FULL, ESP_DISPLAY_PRESENT_ROTATE_180, false, -1, ESP_OK, 3},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_OK, 2},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_DOUBLE_DIRECT, ESP_DISPLAY_PRESENT_ROTATE_180, false, -1, ESP_OK, 2},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_DOUBLE_PARTIAL, ESP_DISPLAY_PRESENT_ROTATE_270, false, -1, ESP_OK, 2},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_TRIPLE_PARTIAL, ESP_DISPLAY_PRESENT_ROTATE_90, false, -1, ESP_OK, 3},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_OK, 3},
        {ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_ROTATE_90, false, -1, ESP_OK, 2},
        {ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_OK, 3},
        {ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI, ESP_DISPLAY_PRESENT_MODE_TE_SYNC, ESP_DISPLAY_PRESENT_ROTATE_0, true, 4, ESP_ERR_NOT_SUPPORTED, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_OK, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_NONE, ESP_DISPLAY_PRESENT_ROTATE_90, false, -1, ESP_OK, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC, ESP_DISPLAY_PRESENT_ROTATE_90, true, 0, ESP_OK, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC, ESP_DISPLAY_PRESENT_ROTATE_0, false, 4, ESP_ERR_NOT_SUPPORTED, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_TE_SYNC, ESP_DISPLAY_PRESENT_ROTATE_0, true, -1, ESP_ERR_NOT_SUPPORTED, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_90, true, 4, ESP_OK, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_90, true, -1, ESP_OK, 0},
        {ESP_DISPLAY_PRESENT_PANEL_IO, ESP_DISPLAY_PRESENT_MODE_DOUBLE_FULL, ESP_DISPLAY_PRESENT_ROTATE_0, true, 4, ESP_ERR_NOT_SUPPORTED, 0},
        {(esp_display_present_panel_t)99, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_ERR_INVALID_ARG, 0},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, (esp_display_present_mode_t)99, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_ERR_INVALID_ARG, 0},
        {ESP_DISPLAY_PRESENT_PANEL_RGB, ESP_DISPLAY_PRESENT_MODE_NONE, (esp_display_present_rotation_t)45, false, -1, ESP_ERR_INVALID_ARG, 0},
        {ESP_DISPLAY_PRESENT_PANEL_AUTO, ESP_DISPLAY_PRESENT_MODE_AUTO, ESP_DISPLAY_PRESENT_ROTATE_0, false, -1, ESP_ERR_NOT_SUPPORTED, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const esp_display_present_target_config_t config = {
            .hw = {
                .panel_type = cases[i].panel,
                .rotation = cases[i].rotation,
                .te_enabled = cases[i].te_enabled,
                .te_sync = {.gpio_num = cases[i].te_gpio},
            },
            .fb = {.mode = cases[i].mode},
        };
        uint8_t count = UINT8_MAX;
        TEST_ASSERT_EQUAL(cases[i].result,
                          esp_display_present_get_required_frame_buffer_count(&config, &count));
        TEST_ASSERT_EQUAL_UINT8(cases[i].count, count);
    }
}

TEST_CASE("framebuffer query ignores resources and clears errors",
          "[present][config]")
{
    esp_display_present_target_config_t config = {
        .hw = {
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
            .input_pixel_format = (esp_display_present_pixel_format_t)99,
        },
        .fb = {
            .mode = ESP_DISPLAY_PRESENT_MODE_AUTO,
            .frame_buffer_count = UINT8_MAX,
        },
        .drawbuf = {.buffers = UINT8_MAX, .te_compose_buffers = UINT8_MAX},
    };
    uint8_t count = UINT8_MAX;
    TEST_ESP_OK(esp_display_present_get_required_frame_buffer_count(&config, &count));
    TEST_ASSERT_EQUAL_UINT8(3, count);
    TEST_ESP_ERR(ESP_ERR_INVALID_ARG,
                 esp_display_present_get_required_frame_buffer_count(NULL, &count));
    TEST_ASSERT_EQUAL_UINT8(0, count);
    TEST_ESP_ERR(ESP_ERR_INVALID_ARG,
                 esp_display_present_get_required_frame_buffer_count(&config, NULL));
}

TEST_CASE("geometry remains a public linked API", "[present][geometry]")
{
    esp_display_present_area_t area = {1, 1, 2, 2}, output;
    esp_display_present_render_alignment_t alignment = {.x_pixels = 2, .y_pixels = 2};
    TEST_ESP_OK(esp_display_present_geometry_align_area((esp_display_present_size_t) {
        8, 8
    }, &area, &alignment, &output));
    TEST_ASSERT_EQUAL(0, output.x1);
    TEST_ASSERT_EQUAL(0, output.y1);
}
void setUp(void) {}
void tearDown(void) {}
void app_main(void)
{
    unity_run_menu();
}
