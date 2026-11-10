#include "esp_lcd_present_trace.h"
#include <assert.h>
int main(void)
{
    char pixels[3][16] = {0}; void *pool[] = {pixels[0], pixels[1], pixels[2]};
    void *panel = pixels; esp_lcd_present_trace_snapshot_t s;
    esp_lcd_present_trace_bind(panel, pool, 3, 9600, 1);
    esp_lcd_present_trace_rgb_supply(panel, 32, 48, 1);
    esp_lcd_present_trace_snapshot(&s);
    assert(s.supply_short == 1 && s.measured_short == 0 && s.measured_frames == 0);
    esp_lcd_present_trace_mark_running(1);
    esp_lcd_present_trace_rgb_supply(panel, 32, 48, 1); /* Partial leading frame: excluded. */
    esp_lcd_present_trace_rgb_supply(panel, 48, 48, 0);
    esp_lcd_present_trace_rgb_supply(panel, 47, 48, 1);
    esp_lcd_present_trace_rgb_supply((void *)1, 0, 48, 1);
    esp_lcd_present_trace_mark_running(0);
    esp_lcd_present_trace_rgb_supply(panel, 46, 48, 1);
    esp_lcd_present_trace_snapshot(&s);
    assert(s.supply_frames == 5 && s.supply_short == 4 && s.supply_restarts == 4);
    assert(s.measured_frames == 2 && s.measured_short == 1 && s.measured_restarts == 1);
    esp_lcd_present_trace_reset(); esp_lcd_present_trace_snapshot(&s);
    assert(!s.measuring && !s.supply_frames && !s.measured_frames && !s.measured_short);
    esp_lcd_present_trace_rgb_supply(panel, 47, 48, 1);
    esp_lcd_present_trace_snapshot(&s); assert(s.supply_short == 1 && s.measured_short == 0);
    for (unsigned i = 0; i < sizeof(pixels); ++i) assert(((char *)pixels)[i] == 0);
}
