/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "platform/lcd/present_lcd_io.h"
#include "platform/present_os.h"
#include "esp_cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

struct fake_lcd {
    esp_lcd_panel_io_color_trans_done_cb_t done;
    void *user;
};
static pthread_barrier_t rendezvous;
static _Thread_local unsigned submit_locks;
static _Thread_local bool arm_submit;
static atomic_uint completions;
static const void *sources[2];
static unsigned draws;

void __real_present_lock_enter(present_lock_t *, bool);
void __wrap_present_lock_enter(present_lock_t *lock, bool isr)
{
    __real_present_lock_enter(lock, isr);
    /* begin_call, preflight, then enqueue. Hold the adapter lock while the
     * first completion starts, without ordering its mode read after this write. */
    if (arm_submit && ++submit_locks == 3) {
        int result = pthread_barrier_wait(&rendezvous);
        assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    }
}
bool esp_ptr_internal(const void *p)
{
    return p != NULL;
}
bool esp_ptr_dma_capable(const void *p)
{
    return p != NULL;
}
bool esp_ptr_dma_ext_capable(const void *p)
{
    (void)p;
    return false;
}
esp_err_t esp_cache_msync(void *p, size_t size, int flags)
{
    (void)flags; assert(p && size); return ESP_OK;
}
esp_err_t esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t io,
                                                    const esp_lcd_panel_io_callbacks_t *callbacks, void *user)
{
    io->done = callbacks->on_color_trans_done; io->user = user; return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x, int y, int ex, int ey, const void *pixels)
{
    (void)panel; assert(x == 0 && y == 0 && ex == 4 && ey == 4);
    assert(draws < 2); sources[draws++] = pixels; return ESP_OK;
}
static bool done(void *user, const esp_display_present_backend_event_t *event, bool isr)
{
    (void)user; assert(isr && event->kind == ESP_DISPLAY_PRESENT_EVENT_TRANSFER_DONE);
    unsigned n = atomic_fetch_add(&completions, 1);
    assert(n < 2 && event->transaction_cookie == n + 1);
    /* quiesce closes the adapter before the second late completion. */
    assert(event->result == (n ? ESP_ERR_INVALID_STATE : ESP_OK));
    const unsigned char *source = sources[n];
    for (size_t i = 0; i < 32; ++i) {
        assert(source[i] == n + 1);
    }
    return false;
}
static void *complete_first(void *user)
{
    struct fake_lcd *lcd = user;
    int result = pthread_barrier_wait(&rendezvous);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    (void)lcd->done(lcd, NULL, lcd->user);
    return NULL;
}
int main(void)
{
    for (unsigned round = 0; round < 100; ++round) {
        struct fake_lcd lcd = {0}; draws = 0; atomic_store(&completions, 0);
        esp_display_present_lcd_io_config_t cfg = {
            .panel = &lcd, .io = &lcd, .format = ESP_DISPLAY_PRESENT_FORMAT_RGB565,
            .max_inflight = 2, .region_mode = true, .staging_bytes = 64
        };
        esp_display_present_lcd_io_t *adapter; esp_display_present_panel_adapter_t api;
        assert(esp_display_present_lcd_io_create(&cfg, &adapter) == ESP_OK);
        assert(esp_display_present_lcd_io_get_interface(adapter, &api) == ESP_OK);
        assert(api.attach(api.context, done, NULL) == ESP_OK);
        _Alignas(64) unsigned char pixels[2][64];
        memset(pixels[0], 1, 64); memset(pixels[1], 2, 64);
        esp_display_present_surface_t surface = {pixels[0], 64, {4, 4, 8, ESP_DISPLAY_PRESENT_FORMAT_RGB565, ESP_DISPLAY_PRESENT_ORDER_NATIVE}};
        esp_display_present_rect_t area = {0, 0, 4, 4};
        assert(api.emit(api.context, &surface, &area, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 1).acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED);
        assert(!pthread_barrier_init(&rendezvous, NULL, 2));
        pthread_t callback; assert(!pthread_create(&callback, NULL, complete_first, &lcd));
        surface.pixels = pixels[1]; submit_locks = 0; arm_submit = true;
        assert(api.emit(api.context, &surface, &area, ESP_DISPLAY_PRESENT_ENGINE_ROTATE_0, 2).acceptance == ESP_DISPLAY_PRESENT_TX_ACCEPTED);
        arm_submit = false;
        assert(!pthread_join(callback, NULL));
        assert(draws == 2 && atomic_load(&completions) == 1);
        assert(api.quiesce(api.context, 0) == ESP_ERR_TIMEOUT);
        assert(esp_display_present_lcd_io_destroy(adapter) == ESP_ERR_INVALID_STATE);
        (void)lcd.done(&lcd, NULL, lcd.user);
        assert(atomic_load(&completions) == 2);
        assert(api.quiesce(api.context, 0) == ESP_OK);
        assert(esp_display_present_lcd_io_destroy(adapter) == ESP_OK);
        assert(!pthread_barrier_destroy(&rendezvous));
    }
    puts("PASS IO callback/submit overlap: FIFO, source lifetime, late completion and bounded quiesce");
    return 0;
}
