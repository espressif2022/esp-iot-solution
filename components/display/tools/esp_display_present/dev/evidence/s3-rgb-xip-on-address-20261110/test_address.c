#include <assert.h>
#include "esp_lcd_present_trace.h"
static uintptr_t actual,target;static unsigned position;
uintptr_t esp_lcd_present_trace_rgb_source(void *panel,uintptr_t *t,unsigned *p){assert(panel==(void*)0x1234);*t=target;*p=position;return actual;}
#include "esp_lcd_present_trace.c"
int main(void){void *pool[]={(void*)0x1000,(void*)0x2000,(void*)0x3000};esp_lcd_present_trace_bind((void*)0x1234,pool,3,9600,1);actual=0x1000;target=0x2000;
 esp_lcd_present_trace_mark_running(1);esp_lcd_present_trace_audit(pool[0],1);assert(audit.releases==0);
 esp_lcd_present_trace_rgb_supply((void*)0x1234,48,48,0);
 esp_lcd_present_trace_audit(pool[1],1);assert(audit.releases==1 && audit.release_active==0);
 esp_lcd_present_trace_audit(pool[0],1);assert(audit.release_active==1);
 esp_lcd_present_trace_audit(pool[0],2);assert(audit.write_active==1);
 esp_lcd_present_trace_audit(pool[1],3);assert(audit.confirm_wrong==1); /* Target matches software, actual independent. */
 actual=target;esp_lcd_present_trace_audit(pool[1],3);assert(audit.confirm_wrong==1 && audit.confirms==2);
 esp_lcd_present_trace_audit((void*)0x9999,2);assert(audit.not_pool==1);
 actual=0;esp_lcd_present_trace_audit(pool[1],3);assert(audit.raced==1);
 esp_lcd_present_trace_mark_running(0);esp_lcd_present_trace_audit(pool[0],1);assert(audit.releases==2);
 esp_lcd_present_trace_reset();assert(!audit.releases && !audit.saved);return 0;}
