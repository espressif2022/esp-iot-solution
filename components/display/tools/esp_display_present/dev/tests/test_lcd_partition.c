/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "platform/present_lcd_rgb_partition.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_rgb.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define OK(x) assert((x) == ESP_OK)

struct fake_lcd {
    rgb_cb_t rgb;
    dpi_cb_t dsi;
    void *user;
    bool early;
    bool draw_fail;
    bool detach_fail;
    unsigned draws;
};

esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
                                                      const esp_lcd_rgb_panel_event_callbacks_t *callbacks,
                                                      void *user)
{
    if (!user && panel->detach_fail) {
        return ESP_FAIL;
    }
    assert(!callbacks->on_frame_buf_complete && !callbacks->on_vsync);
    panel->rgb = callbacks->on_color_trans_done;
    panel->user = user;
    return ESP_OK;
}

esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t panel,
                                                      const esp_lcd_dpi_panel_event_callbacks_t *callbacks,
                                                      void *user)
{
    if (!user && panel->detach_fail) {
        return ESP_FAIL;
    }
    assert(!callbacks->on_frame_buf_complete && !callbacks->on_vsync);
    panel->dsi = callbacks->on_color_trans_done;
    panel->user = user;
    return ESP_OK;
}

static void complete(struct fake_lcd *panel)
{
    if (panel->rgb) {
        (void)panel->rgb(panel, NULL, panel->user);
    }
    if (panel->dsi) {
        (void)panel->dsi(panel, NULL, panel->user);
    }
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x, int y,
                                    int x_end, int y_end, const void *pixels)
{
    assert(x == 1 && y == 1 && x_end == 3 && y_end == 2 && pixels);
    ++panel->draws;
    if (panel->early) {
        complete(panel);
    }
    return panel->draw_fail ? ESP_FAIL : ESP_OK;
}

typedef struct {
    unsigned calls;
    uint64_t cookie;
} completion_t;

static bool on_event(void *user, const esp_display_present_backend_event_t *event, bool isr)
{
    completion_t *completion = user;
    assert(isr && event->kind == ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE && event->result == ESP_OK);
    ++completion->calls;
    completion->cookie = event->transaction_cookie;
    return false;
}

static void exercise(esp_display_present_lcd_partition_kind_t kind)
{
    struct fake_lcd panel = {0};
    esp_display_present_lcd_rgb_partition_config_t config = {
        .panel = &panel, .kind = kind, .width = 4, .height = 3,
        .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
        .byte_order = ESP_DISPLAY_PRESENT_ORDER_NATIVE,
    };
    esp_display_present_lcd_rgb_partition_t *adapter = NULL;
    OK(esp_display_present_lcd_rgb_partition_create(&config, &adapter));
    esp_display_present_output_caps_t caps;
    OK(esp_display_present_lcd_rgb_partition_get_output_caps(adapter, &caps));
    assert(caps.internal_dma && caps.ext_mem && caps.max_inflight == 1 && caps.tight_stride);
    esp_display_present_panel_adapter_t api;
    OK(esp_display_present_lcd_rgb_partition_get_interface(adapter, &api));
    completion_t completion = {0};
    OK(api.attach(api.context, on_event, &completion));
    assert(api.attach(api.context, on_event, &completion) == ESP_ERR_INVALID_STATE);

    uint16_t pixels[2] = {0x1234, 0x5678};
    esp_display_present_surface_t surface = {
        pixels, sizeof(pixels), {2, 1, 4, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}
    };
    esp_display_present_rect_t area = {1, 1, 2, 1};
    esp_display_present_tx_result_t tx = api.submit(api.context, &surface, &area, 41);
    assert(tx.acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED && tx.result == ESP_OK);
    assert(api.submit(api.context, &surface, &area, 42).acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED);
    complete(&panel);
    assert(completion.calls == 1 && completion.cookie == 41);
    complete(&panel);
    assert(completion.calls == 1);

    panel.early = true;
    tx = api.submit(api.context, &surface, &area, 43);
    assert(tx.acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED && completion.calls == 2 && completion.cookie == 43);
    panel.early = false;

    esp_display_present_surface_t invalid = surface;
    invalid.layout.stride_bytes = 6;
    assert(api.submit(api.context, &invalid, &area, 44).acceptance == ESP_DISPLAY_PRESENT_TX_REJECTED);

    panel.detach_fail = true;
    assert(api.quiesce(api.context, 0) == ESP_FAIL);
    assert(esp_display_present_lcd_rgb_partition_destroy(adapter) == ESP_ERR_INVALID_STATE);
    panel.detach_fail = false;
    OK(api.quiesce(api.context, 0));
    assert(!panel.rgb && !panel.dsi);
    OK(esp_display_present_lcd_rgb_partition_destroy(adapter));
}

int main(void)
{
    assert(esp_display_present_lcd_rgb_partition_create(NULL, NULL) == ESP_ERR_INVALID_ARG);
    struct fake_lcd panel = {0};
    esp_display_present_lcd_rgb_partition_config_t invalid = {
        .panel = &panel, .kind = ESP_DISPLAY_PRESENT_LCD_PARTITION_RGB,
        .width = 4, .height = 3, .format = (esp_display_present_engine_pixel_format_t)99,
        .byte_order = ESP_DISPLAY_PRESENT_ORDER_NATIVE,
    };
    esp_display_present_lcd_rgb_partition_t *adapter = (void *)1;
    assert(esp_display_present_lcd_rgb_partition_create(&invalid, &adapter) == ESP_ERR_INVALID_ARG && adapter == (void *)1);
    invalid.format = ESP_DISPLAY_PRESENT_FORMAT_RGB888;
    invalid.byte_order = ESP_DISPLAY_PRESENT_ORDER_SWAPPED;
    assert(esp_display_present_lcd_rgb_partition_create(&invalid, &adapter) == ESP_ERR_INVALID_ARG && adapter == (void *)1);
    exercise(ESP_DISPLAY_PRESENT_LCD_PARTITION_RGB);
    exercise(ESP_DISPLAY_PRESENT_LCD_PARTITION_DSI);
    puts("lcd partition: RGB/DSI attach, early completion, validation and detach retry passed");
    return 0;
}
