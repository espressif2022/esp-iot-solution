#include <string.h>
/* Diagnostic only. Switch dispatch at case boundaries; no scanout changes. */
#include "bench_api.h"
#include "esp_attr.h"
#ifndef BENCH_ADAPTER
#include "pixel/present_executor.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static bool inline_mode;
void bench_diag_mode(bool value){inline_mode=value;}
bool IRAM_ATTR bench_diag_inline(void){return inline_mode;}
void bench_probe_execution(uint64_t,uint64_t,unsigned,bool,bool);
#ifndef BENCH_ADAPTER
esp_err_t __real_esp_display_present_executor_run(const esp_display_present_executor_dispatch_t*,const esp_display_present_copy_request_t*,esp_display_present_executor_selection_t*);
esp_err_t __wrap_esp_display_present_executor_run(const esp_display_present_executor_dispatch_t*d,const esp_display_present_copy_request_t*r,esp_display_present_executor_selection_t*out){
 esp_display_present_executor_selection_t local={0};esp_display_present_executor_selection_t *s=out?out:&local;
 uint64_t start=esp_timer_get_time();esp_err_t ret=__real_esp_display_present_executor_run(d,r,s);uint64_t elapsed=esp_timer_get_time()-start;
 esp_display_present_executor_caps_t caps={0};bool hw=false;
 if(s->executor&&esp_display_present_executor_get_caps(s->executor,&caps)==ESP_OK)hw=caps.name&&strcmp(caps.name,"cpu")!=0;
 bench_probe_execution(elapsed,(uint64_t)r->src_area.width*r->src_area.height*2,xPortGetCoreID(),hw,ret!=ESP_OK);
 return ret;
}
#endif
