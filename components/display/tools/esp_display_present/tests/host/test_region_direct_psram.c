/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#define PRESENT_HOST_CUSTOM_MEMORY_POINTERS 1
static void observe_draw(int x, int y, int ex, int ey, const void *pixels);
#define PRESENT_HOST_DRAW_OBSERVER observe_draw
#include "support/legacy_panel.h"
#include "core/present_engine.h"
#include "platform/present_os.h"
#include "esp_memory_utils.h"

typedef struct {
    void *pixels;
    size_t bytes;
    bool external;
} allocation_t;
static allocation_t allocations[64];
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static size_t psram_limit = SIZE_MAX;
static unsigned fail_at, pixel_allocations;
static unsigned rotation, logical_width, logical_height, bytes_per_pixel;
static bool swapped, expected_external;
static size_t observed_pixels;

bool __wrap_present_psram_can_fit(size_t bytes, size_t count)
{
    return bytes && count <= psram_limit / bytes;
}
void *__real_present_alloc_pixels_in(size_t bytes, bool external);
void *__wrap_present_alloc_pixels_in(size_t bytes, bool external)
{
    if (fail_at && ++pixel_allocations == fail_at) {
        return NULL;
    }
    void *pixels = __real_present_alloc_pixels_in(bytes, external);
    if (pixels) {
        pthread_mutex_lock(&allocation_lock);
        size_t i;
        for (i = 0; i < 64 && allocations[i].pixels; ++i) {}
        assert(i < 64);
        allocations[i] = (allocation_t) {
            pixels, bytes, external
        };
        pthread_mutex_unlock(&allocation_lock);
    }
    return pixels;
}
void __real_present_free(void *pixels);
void __wrap_present_free(void *pixels)
{
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < 64; ++i) {
        if (allocations[i].pixels == pixels) {
            allocations[i] = (allocation_t) {
                0
            };
        }
    }
    pthread_mutex_unlock(&allocation_lock);
    __real_present_free(pixels);
}
static size_t allocation_count(bool external)
{
    size_t count = 0;
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < 64; ++i) {
        count += allocations[i].pixels && allocations[i].external == external;
    }
    pthread_mutex_unlock(&allocation_lock);
    return count;
}
static bool external_address(const void *pixels)
{
    uintptr_t address = (uintptr_t)pixels;
    bool external = false;
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < 64; ++i) {
        uintptr_t begin = (uintptr_t)allocations[i].pixels;
        if (begin && address >= begin && address - begin < allocations[i].bytes) {
            external = allocations[i].external;
            break;
        }
    }
    pthread_mutex_unlock(&allocation_lock);
    return external;
}
bool esp_ptr_internal(const void *pixels)
{
    return pixels && !external_address(pixels);
}
bool esp_ptr_dma_capable(const void *pixels)
{
    return esp_ptr_internal(pixels);
}
bool esp_ptr_dma_ext_capable(const void *pixels)
{
    return pixels && external_address(pixels);
}

static void pixel_value(unsigned x, unsigned y, uint8_t *value)
{
    value[0] = (uint8_t)x;
    value[1] = (uint8_t)y;
    value[2] = (uint8_t)(x + y);
}
static void observe_draw(int x, int y, int ex, int ey, const void *pixels)
{
    assert(external_address(pixels) == expected_external);
    const uint8_t *data = pixels;
    for (int py = y; py < ey; ++py) {
        for (int px = x; px < ex; ++px) {
            unsigned lx = px, ly = py;
            if (rotation == 1) {
                lx = py;
                ly = logical_height - 1 - px;
            } else if (rotation == 2) {
                lx = logical_width - 1 - px;
                ly = logical_height - 1 - py;
            } else if (rotation == 3) {
                lx = logical_width - 1 - py;
                ly = px;
            }
            uint8_t value[3];
            pixel_value(lx, ly, value);
            for (unsigned b = 0; b < bytes_per_pixel; ++b) {
                assert(*data++ == value[swapped ? 1 - b : b]);
            }
            ++observed_pixels;
        }
    }
}
static esp_display_presenter_config_t input_for(struct fake_lcd *panel, unsigned width, unsigned height, unsigned lines)
{
    esp_display_presenter_config_t input = config(panel, false);
    input.width = width;
    input.height = height;
    input.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    input.target.hw.io = panel;
    input.target.hw.spi_psram_dma_direct = panel->psram_direct;
    input.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    input.target.drawbuf.in_psram = true;
    input.target.drawbuf.buffers = 2;
    input.target.drawbuf.lines = lines;
    return input;
}
static void finish_transfer(struct fake_lcd *panel, unsigned before)
{
    uint64_t start = present_now_ms();
    pthread_mutex_lock(&panel->lock);
    while (panel->calls == before) {
        pthread_mutex_unlock(&panel->lock);
        assert(!present_expired(start, 1000));
        present_pause();
        pthread_mutex_lock(&panel->lock);
    }
    esp_lcd_panel_io_color_trans_done_cb_t done = panel->io;
    void *user = panel->user;
    pthread_mutex_unlock(&panel->lock);
    assert(done);
    (void)done(panel, NULL, user);
}
static void fill(esp_display_present_lease_t *lease, unsigned x, unsigned y, unsigned width, unsigned rows)
{
    uint8_t *data = lease->surface.pixels;
    for (unsigned row = 0; row < rows; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            uint8_t value[3];
            pixel_value(x + col, y + row, value);
            memcpy(data, value, bytes_per_pixel);
            data += bytes_per_pixel;
        }
    }
}
static void exercise(unsigned width, unsigned height, unsigned lines, bool direct, bool swap, bool rgb888, bool limited, unsigned initial_rotation)
{
    struct fake_lcd panel;
    init_panel(&panel);
    panel.physical_width = width;
    panel.physical_height = height;
    panel.psram_direct = direct;
    bytes_per_pixel = rgb888 ? 3 : 2;
    psram_limit = limited ? (size_t)width * lines * bytes_per_pixel * 2 : SIZE_MAX;
    expected_external = direct && !limited && lines >= 32 && !swap && !initial_rotation;
    swapped = swap;
    bool quarter = (initial_rotation & 1) != 0;
    esp_display_presenter_config_t input = input_for(&panel, quarter ? height : width, quarter ? width : height, lines);
    input.target.hw.rotation = initial_rotation * 90;
    input.target.hw.swap_bytes = swap;
    input.pixel_format = input.target.hw.input_pixel_format = rgb888 ? ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB888 : ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565;
    present_resources_t *resources;
    OK(present_resources_create(&input, false, NULL, NULL, &resources));
    esp_display_present_engine_t *engine = present_resources_presenter(resources);
    present_resource_status_t status;
    OK(present_resources_status(resources, &status));
    assert(status.plan.normalized.backend.tx_staging == !expected_external);
    assert(status.plan.conversion.count == 2); /* Peak includes lazy rotation. */
    assert(status.plan.conversion.memory == (expected_external ? ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL : ESP_DISPLAY_PRESENT_MEMORY_INTERNAL_DMA));
    if (expected_external && !swap && !initial_rotation) {
        assert(allocation_count(true) == 2 && allocation_count(false) == 0);
        assert(!status.plan.normalized.backend.async_copy && status.plan.stack_bytes == 0);
    }
    if (!expected_external) {
        assert(status.plan.normalized.backend.async_copy);
        assert(allocation_count(true) == 2 && allocation_count(false) == 2);
    }
    size_t peak = status.plan.pixel_bytes;
    for (unsigned turn = 0; turn < 5; ++turn) {
        rotation = (initial_rotation + turn) % 4;
        OK(esp_display_present_engine_set_rotation(engine, (esp_display_present_engine_rotation_t)rotation, 1000));
        esp_display_present_backend_caps_t caps;
        OK(esp_display_present_engine_get_caps(engine, &caps));
        logical_width = caps.draw_layout.width;
        logical_height = caps.draw_layout.height;
        esp_display_present_frame_request_t request = {0};
        OK(esp_display_present_engine_begin_next_frame(engine, &request));
        observed_pixels = 0;
        for (unsigned y = 0; y < logical_height;) {
            esp_display_present_lease_t lease;
            OK(esp_display_present_engine_acquire_buffer(engine, &lease));
            uint32_t rows;
            OK(esp_display_present_engine_resolve_rows(engine, &lease, logical_width * bytes_per_pixel, logical_height - y, &rows));
            assert(rows);
            fill(&lease, 0, y, logical_width, rows);
            unsigned before = panel.calls;
            esp_display_present_engine_area_t area = {0, y, logical_width - 1, y + rows - 1};
            OK(esp_display_present_engine_submit_buffer(engine, &lease, &area, logical_width * bytes_per_pixel));
            finish_transfer(&panel, before);
            y += rows;
        }
        OK(esp_display_present_engine_commit_frame(engine, NULL));
        assert(observed_pixels == width * height);
        OK(present_resources_status(resources, &status));
        assert(status.plan.pixel_bytes == peak);
        if (expected_external && rotation) {
            assert(allocation_count(true) == 4 && allocation_count(false) == 0);
        }
    }
    rotation = 0;
    OK(esp_display_present_engine_set_rotation(engine, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1000));
    esp_display_present_backend_caps_t caps;
    OK(esp_display_present_engine_get_caps(engine, &caps));
    logical_width = caps.draw_layout.width;
    logical_height = caps.draw_layout.height;
    /* Small dirty areas must be expanded before rendering, never padded on wire. */
    esp_display_present_rect_t dirty = {1, 1, 1, 1}, render;
    OK(esp_display_present_engine_resolve_render_area(engine, &dirty, &render));
    assert(render.width == (expected_external ? rgb888 ? 64u : 32u : 1u));
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(engine, &request));
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(engine, &lease));
    fill(&lease, render.x, render.y, render.width, render.height);
    unsigned before = panel.calls;
    esp_display_present_engine_area_t area = {render.x, render.y, render.x + render.width - 1, render.y + render.height - 1};
    OK(esp_display_present_engine_submit_buffer(engine, &lease, &area, render.width * bytes_per_pixel));
    finish_transfer(&panel, before);
    OK(esp_display_present_engine_cancel_frame(engine));
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
    psram_limit = SIZE_MAX;
    assert(!allocation_count(true) && !allocation_count(false));
}
static void late_completion(bool stop)
{
    struct fake_lcd panel;
    init_panel(&panel);
    panel.physical_width = panel.physical_height = 64;
    panel.psram_direct = true;
    expected_external = true;
    bytes_per_pixel = 2;
    rotation = 0;
    logical_width = logical_height = 64;
    swapped = false;
    esp_display_presenter_config_t input = input_for(&panel, 64, 64, 32);
    input.target.drawbuf.buffers = 1;
    input.pipeline_acquire_timeout_ms = 1;
    present_resources_t *resources;
    OK(present_resources_create(&input, false, NULL, NULL, &resources));
    esp_display_present_engine_t *engine = present_resources_presenter(resources);
    esp_display_present_frame_request_t request = {0};
    OK(esp_display_present_engine_begin_next_frame(engine, &request));
    esp_display_present_lease_t lease;
    OK(esp_display_present_engine_acquire_buffer(engine, &lease));
    fill(&lease, 0, 0, 64, 32);
    esp_display_present_engine_area_t area = {0, 0, 63, 31};
    OK(esp_display_present_engine_submit_buffer(engine, &lease, &area, 128));
    assert(panel.source == lease.surface.pixels && allocation_count(true) == 1);
    uint8_t saved[64];
    memcpy(saved, panel.source, sizeof(saved));
    if (stop) {
        assert(present_resources_stop(resources, 1) == ESP_ERR_TIMEOUT);
    } else {
        assert(esp_display_present_engine_cancel_frame(engine) == ESP_ERR_TIMEOUT);
    }
    assert(allocation_count(true) == 1 && !memcmp(saved, panel.source, sizeof(saved)));
    finish_transfer(&panel, 0);
    if (!stop) {
        OK(esp_display_present_engine_cancel_frame(engine));
    }
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
    assert(!allocation_count(true) && !allocation_count(false));
}
static void retry_single_tile(bool change_path)
{
    struct fake_lcd panel;
    init_panel(&panel);
    panel.psram_direct = true;
    esp_display_presenter_config_t input = input_for(&panel, 64, 64, 32);
    input.target.drawbuf.buffers = 0;
    pixel_allocations = 0;
    psram_limit = change_path ? 64 * 32 * 2 * 2 : SIZE_MAX;
    fail_at = change_path ? 3 : 2;
    present_resources_t *resources;
    OK(present_resources_create(&input, false, NULL, NULL, &resources));
    fail_at = 0;
    present_resource_status_t status;
    OK(present_resources_status(resources, &status));
    assert(status.plan.tiles.count == 1 && status.plan.conversion.count == 1);
    assert(!status.plan.normalized.backend.tx_staging);
    assert(status.plan.conversion.memory == ESP_DISPLAY_PRESENT_MEMORY_EXTERNAL);
    assert(allocation_count(true) == 1 && !allocation_count(false));
    OK(present_resources_destroy(resources, 1000));
    free_panel(&panel);
    psram_limit = SIZE_MAX;
    assert(!allocation_count(true) && !allocation_count(false));
}
int main(void)
{
    exercise(64, 96, 48, true, false, false, false, 0);
    exercise(64, 96, 48, true, true, false, false, 0);
    exercise(128, 96, 64, true, false, true, false, 0);
    exercise(64, 64, 16, true, false, false, false, 0);
    exercise(64, 64, 32, false, false, false, false, 0);
    exercise(64, 64, 32, true, false, false, true, 0);
    exercise(64, 96, 48, true, false, false, false, 1);
    exercise(64, 96, 48, true, true, false, false, 3);
    late_completion(false);
    late_completion(true);
    retry_single_tile(false);
    retry_single_tile(true); /* Initial staging can become direct after shrink. */
    return 0;
}
