#include "present_lcd_scanout.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_cache.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
struct fake_lcd { void *buffers[2]; dpi_cb_t cb; void *user; const void *target; uint64_t generation; int early; };
esp_err_t esp_lcd_dpi_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_dpi_panel_event_callbacks_t *c, void *u) { p->cb=c->on_frame_buf_complete; p->user=u; return ESP_OK; }
esp_err_t esp_lcd_dpi_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **first, ...) { assert(n==2); *first=p->buffers[0]; va_list v; va_start(v,first); *va_arg(v,void **)=p->buffers[1]; va_end(v); return ESP_OK; }
esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t p, const esp_lcd_rgb_panel_event_callbacks_t *c, void *u) {(void)p;(void)c;(void)u;return ESP_FAIL;}
esp_err_t esp_lcd_rgb_panel_get_frame_buffer(esp_lcd_panel_handle_t p, uint32_t n, void **first, ...) {(void)p;(void)n;(void)first;return ESP_FAIL;}
esp_err_t esp_lcd_rgb_panel_get_frame_buffer_info(esp_lcd_panel_handle_t p, esp_lcd_rgb_panel_frame_buffer_info_t *i) {(void)p;(void)i;return ESP_FAIL;}
esp_err_t esp_cache_msync(void *p,size_t n,int f) {(void)p;(void)n;(void)f;return ESP_OK;}
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t p) {(void)p;return ESP_OK;}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p,int a,int b,int c,int d,const void *s) {(void)p;(void)a;(void)b;(void)c;(void)d;(void)s;abort();}
static void fire(struct fake_lcd *p,uint64_t gen,const void *target) { esp_lcd_dpi_panel_event_data_t e={.present_generation=gen,.selected_frame_buffer=target,.retired_frame_buffer=p->buffers[gen == 11 ? 1 : 0]}; p->cb(p,&e,p->user); }
esp_err_t esp_lcd_dpi_panel_select_frame_buffer_tagged(esp_lcd_panel_handle_t p,const void *fb,uint64_t gen) {p->target=fb;p->generation=gen;if(p->early)fire(p,gen,fb);return ESP_OK;}
static bool done(void *u,const esp_display_present_backend_event_t *e,bool isr) {unsigned *count=u; assert(isr && e->kind==ESP_DISPLAY_PRESENT_EVENT_SCANOUT_CONFIRMED); ++*count; return false;}
int main(void) {struct fake_lcd p={0};p.buffers[0]=aligned_alloc(64,64);p.buffers[1]=aligned_alloc(64,64);assert(p.buffers[0]&&p.buffers[1]); esp_display_present_lcd_scanout_config_t c={.panel=&p,.kind=ESP_DISPLAY_PRESENT_LCD_DSI,.width=3,.height=2,.format=ESP_DISPLAY_PRESENT_FORMAT_RGB565,.buffer_count=2,.delete_panel=true};esp_display_present_lcd_scanout_t *a;assert(esp_display_present_lcd_scanout_create(&c,&a)==ESP_OK);esp_display_present_panel_adapter_t api;assert(esp_display_present_lcd_scanout_get_interface(a,&api)==ESP_OK);unsigned count=0;assert(api.attach(api.context,done,&count)==ESP_OK);esp_display_present_surface_t s={p.buffers[1],12,{3,2,6,ESP_DISPLAY_PRESENT_FORMAT_RGB565,ESP_DISPLAY_PRESENT_ORDER_NATIVE}};esp_display_present_rect_t area={0,0,3,2};assert(api.submit(api.context,&s,&area,10).acceptance==ESP_DISPLAY_PRESENT_TX_ACCEPTED);fire(&p,9,p.buffers[1]);fire(&p,10,p.buffers[0]);assert(count==0);fire(&p,10,p.buffers[1]);fire(&p,10,p.buffers[1]);assert(count==1);p.early=1;assert(api.submit(api.context,&s,&area,11).acceptance==ESP_DISPLAY_PRESENT_TX_ACCEPTED);assert(count==2);assert(api.quiesce(api.context,100)==ESP_OK);assert(esp_display_present_lcd_scanout_destroy(a)==ESP_OK);free(p.buffers[0]);free(p.buffers[1]);puts("tagged adapter cases passed");}
