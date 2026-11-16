/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#define esp_lcd_panel_draw_bitmap fixture_draw_bitmap
#include "support/legacy_panel.h"
#undef esp_lcd_panel_draw_bitmap
#include "esp_gsp_esp_lcd.h"
#include "esp_lv_present.h"
#include "gsp/gsp_memory.h"
#include <stdatomic.h>

enum { WIDTH = 320, HEIGHT = 240, TIMEOUT_MS = 1000 };
static uint16_t pixels[HEIGHT][WIDTH];
static uint16_t gsp_image[HEIGHT][WIDTH];
static atomic_bool fail_acquire;
static atomic_uint failed_acquires, token_frees;
static atomic_uintptr_t watched_token;

void __real_gsp_memory_free(void *pointer);
void __wrap_gsp_memory_free(void *pointer)
{
    if (pointer && (uintptr_t)pointer == atomic_load(&watched_token)) {
        atomic_fetch_add(&token_frees, 1);
    }
    __real_gsp_memory_free(pointer);
}

esp_err_t __real_esp_display_presenter_acquire_buffer(esp_display_presenter_t *presenter,
                                                      esp_display_presenter_buffer_t *buffer);
esp_err_t __wrap_esp_display_presenter_acquire_buffer(esp_display_presenter_t *presenter,
                                                      esp_display_presenter_buffer_t *buffer)
{
    esp_err_t result = __real_esp_display_presenter_acquire_buffer(presenter, buffer);
    if (atomic_exchange(&fail_acquire, false)) {
        /* Fail after the live Present frame owns a lease. The GSP display
         * adapter must cancel it as part of its failed activation. */
        assert(result == ESP_OK && buffer->lease_id != 0);
        atomic_fetch_add(&failed_acquires, 1);
        return ESP_FAIL;
    }
    return result;
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
                                    int x, int y, int ex, int ey, const void *source)
{
    assert(x >= 0 && y >= 0 && ex <= WIDTH && ey <= HEIGHT && x < ex && y < ey);
    pthread_mutex_lock(&panel->lock);
    const uint16_t *row = source;
    for (int i = y; i < ey; ++i) {
        memcpy(&pixels[i][x], row, (size_t)(ex - x) * sizeof(*row));
        row += ex - x;
    }
    ++panel->calls;
    esp_lcd_panel_io_color_trans_done_cb_t done = panel->io;
    void *user = panel->user;
    pthread_mutex_unlock(&panel->lock);
    assert(done);
    (void)done(panel, NULL, user);
    return ESP_OK;
}

static void load_bundle(const char *path, esp_gsp_config_t *config)
{
    FILE *file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    size_t size = (size_t)length;
    void *data = aligned_alloc(64, (size + 63) & ~(size_t)63);
    assert(data && fread(data, 1, size, file) == size && fclose(file) == 0);
    config->bundle = data;
    config->bundle_size = size;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    struct fake_lcd panel;
    init_panel(&panel);
    esp_display_presenter_config_t present = config(&panel, false);
    present.width = WIDTH;
    present.height = HEIGHT;
    present.target.hw.panel_type = ESP_DISPLAY_PRESENT_PANEL_IO;
    present.target.hw.io = &panel;
    present.target.fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE;
    present.target.drawbuf.lines = 24;
    present.pipeline_acquire_timeout_ms = TIMEOUT_MS;
    present.transfer_timeout_ms = TIMEOUT_MS;
    esp_display_presenter_t *presenter = NULL;
    OK(esp_display_presenter_create(&present, &presenter));

    esp_gsp_config_t app = ESP_GSP_CONFIG_INIT();
    load_bundle(argv[1], &app);
    assert(esp_gsp_config_set(&app, ESP_GSP_FIELD_ENABLE_ASYNC_DECODE, 0) == ESP_GSP_CONFIG_SET_OK);
    esp_gsp_esp_lcd_config_t gsp_config = ESP_GSP_ESP_LCD_CONFIG_INIT();
    gsp_config.presenter = presenter;
    gsp_config.disable_accel = true;
    gsp_config.task_stack_psram = true; /* Selects the joinable shutdown path of the Host task shim. */
    esp_gsp_handle_t gsp = NULL;
    OK(esp_display_presenter_release_producer(presenter));
    OK(esp_gsp_esp_lcd_start(&app, &gsp_config, &gsp));
    assert(gsp);
    esp_gsp_esp_lcd_pause_t *pause = NULL;
    OK(esp_gsp_esp_lcd_pause(gsp, TIMEOUT_MS, &pause));
    assert(pause);
    atomic_store(&watched_token, (uintptr_t)pause);
    pthread_mutex_lock(&panel.lock);
    assert(panel.calls && pixels[0][0] != 0);
    memcpy(gsp_image, pixels, sizeof(gsp_image));
    pthread_mutex_unlock(&panel.lock);

    lv_display_t *display = NULL;
    OK(esp_lv_present_start(presenter, &display));
    assert(display && esp_display_presenter_release_producer(presenter) == ESP_ERR_INVALID_STATE);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xff0000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    assert(esp_lv_present_get_frame_count() == 1);
    OK(esp_display_presenter_quiesce(presenter, TIMEOUT_MS));
    OK(esp_lv_present_stop());
    pthread_mutex_lock(&panel.lock);
    assert(pixels[0][0] == 0xf800 && pixels[HEIGHT - 1][WIDTH - 1] == 0xf800);
    unsigned lvgl_calls = panel.calls;
    pthread_mutex_unlock(&panel.lock);
    OK(esp_display_presenter_release_producer(presenter));

    atomic_store(&fail_acquire, true);
    esp_gsp_handle_t resumed = NULL;
    assert(esp_gsp_esp_lcd_resume_paused(pause, &resumed) == ESP_FAIL);
    assert(!resumed && atomic_load(&failed_acquires) == 1 && atomic_load(&token_frees) == 0);
    size_t area_count = 0;
    bool full = false;
    assert(esp_display_presenter_begin_next_frame(presenter, NULL, NULL, 0, &area_count, &full) ==
           ESP_ERR_INVALID_STATE);
    pthread_mutex_lock(&panel.lock);
    assert(panel.calls == lvgl_calls);
    pthread_mutex_unlock(&panel.lock);

    OK(esp_gsp_esp_lcd_resume_paused(pause, &resumed));
    assert(resumed == gsp && atomic_load(&token_frees) == 1);
    atomic_store(&watched_token, 0);
    assert(esp_gsp_stop(resumed) == ESP_GSP_OK);
    OK(esp_display_presenter_quiesce(presenter, TIMEOUT_MS));
    pthread_mutex_lock(&panel.lock);
    assert(panel.calls > lvgl_calls && memcmp(pixels, gsp_image, sizeof(pixels)) == 0);
    pthread_mutex_unlock(&panel.lock);

    OK(esp_display_presenter_delete(presenter));
    lv_deinit();
    free((void *)app.bundle);
    free_panel(&panel);
    return 0;
}
