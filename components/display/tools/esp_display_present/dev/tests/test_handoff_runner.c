/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include "common.h"
esp_err_t esp_display_presenter_release_producer(esp_display_presenter_t *p)
{
    (void)p;
    return ESP_OK;
}
#include "../../../../../../examples/display/gui/gsp_lvgl_present_handoff/main/handoff_stress_runner.c"
static int mode, resumes, starts, stops, quiesces, paused, lvgl;
static int64_t now;
const char *esp_err_to_name(int e)
{
    return e ? "INJECTED" : "OK";
}
int64_t esp_timer_get_time(void)
{
    return now;
}
void vTaskDelay(unsigned ms)
{
    now += (int64_t)ms * 1000;
}
void lv_timer_handler(void)
{
    now += 1000;
}
void *lv_display_get_screen_active(lv_display_t *d)
{
    return d;
}
void lv_obj_invalidate(void *p)
{
    (void)p;
}
void handoff_lvgl_screen_create(lv_display_t *d, int a, int b)
{
    assert(lvgl);
}
void handoff_lvgl_screen_delete(void)
{
    assert(lvgl);
}
esp_err_t gsp_hello_load_set_value(void *g, int32_t x)
{
    return 0;
}
void *esp_gsp_timer_create(void *g, unsigned n, void (*f)(void *, void *), void *c)
{
    return (void *)1;
}
esp_err_t esp_gsp_esp_lcd_pause(void *g, unsigned n, esp_gsp_esp_lcd_pause_t **p)
{
    paused = 1;
    *p = (void *)2;
    if (mode == 9) {
        paused = 0;
        *p = NULL;
        return ESP_ERR_TIMEOUT;
    } return mode == 1 ? ESP_ERR_NOT_FINISHED : 0;
}
esp_err_t esp_gsp_esp_lcd_resume_paused(esp_gsp_esp_lcd_pause_t *p, void **g)
{
    assert(!lvgl && paused && p == (void *)2);
    resumes++;
    *g = NULL;
    if (mode == 8 || (mode == 7 && resumes == 1)) {
        return ESP_ERR_TIMEOUT;
    } paused = 0;
    *g = (void *)1;
    return 0;
}
esp_err_t esp_display_presenter_quiesce(esp_display_presenter_t *p, unsigned n)
{
    quiesces++;
    return (mode == 3 || (mode == 5 && quiesces == 1)) ? ESP_ERR_TIMEOUT : 0;
}
esp_err_t esp_lv_present_start(esp_display_presenter_t *p, lv_display_t **d)
{
    starts++;
    assert(paused);
    if (mode == 2) {
        return ESP_FAIL;
    } lvgl = 1;
    *d = (void *)3;
    return 0;
}
esp_err_t esp_lv_present_stop(void)
{
    stops++;
    if (mode == 4 || (mode == 6 && stops == 1)) {
        return ESP_ERR_TIMEOUT;
    } lvgl = 0;
    return 0;
}
uint32_t esp_lv_present_get_frame_count(void)
{
    return 10;
}
int main(void)
{
    const int cases[] = {0, 1, 2, 6, 7, 9};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        mode = cases[i];
        resumes = starts = stops = quiesces = paused = lvgl = 0;
        now = 0;
        void *gsp = (void *)1;
        uint32_t errors = 0;
        run_handoff_cycle((void *)4, &gsp, 1, 1, &errors);
        assert(!paused && !lvgl && gsp == (void *)1);
        if (mode == 1) {
            assert(starts == 0 && resumes == 1 && errors == 1);
        } else if (mode == 9) {
            assert(starts == 0 && resumes == 0 && errors == 1);
        } else if (mode == 2) {
            assert(starts == 1 && resumes == 1 && errors == 1);
        } else {
            assert(starts == 1 && resumes == (mode == 7 ? 2 : 1));
        }
    }
    return 0;
}
