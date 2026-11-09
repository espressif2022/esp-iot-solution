from pathlib import Path
import shutil
R=Path('/tmp/present-s31-deep-analysis-20261109'); R.mkdir(exist_ok=True)
old=Path('/tmp/present-s31-rgb-analysis-20261109/baseline/source')
lvgl=Path('/home/xuxin/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.6.0~1_7d824107')
shutil.copytree(old,R/'template',dirs_exist_ok=True)
shutil.copytree(lvgl,R/'lvgl',dirs_exist_ok=True)
p=R/'lvgl/demos/benchmark/lv_demo_benchmark.c';s=p.read_text();s=s.replace('static void load_scene(lv_obj_t * scr, uint32_t scene)\n{','extern void bench_probe_scene_enter(unsigned scene, const char *name);\nstatic void load_scene(lv_obj_t * scr, uint32_t scene)\n{\n    bench_probe_scene_enter(scene, scenes[scene].name);');p.write_text(s)
d=R/'template/components/display/tools/esp_display_present/dev/test_apps/benchmark';p=d/'main/main.c';s=p.read_text().replace('static const char *TAG', 'void bench_probe_install(lv_display_t *);\nvoid bench_probe_dump(void);\nstatic const char *TAG').replace('lv_demo_benchmark_set_end_cb(end_cb);','bench_probe_install(disp);\n        lv_demo_benchmark_set_end_cb(end_cb);').replace('memory_report("after_run", true);','ESP_ERROR_CHECK(bench_lock());\n        bench_probe_dump();\n        bench_unlock();\n        memory_report("after_run", true);');p.write_text(s)
p=d/'main/bridge_adapter.c';s=p.read_text().replace('cfg.profile.buffer_height = 50','cfg.profile.buffer_height = (c->rotation == 90 || c->rotation == 270) ? 30 : 50');p.write_text(s)
p=d/'main/CMakeLists.txt';s=p.read_text().replace('SRCS main.c ${BRIDGE}','SRCS main.c probe.c ${BRIDGE}').replace('hw_init lvgl esp_timer)','hw_init lvgl esp_timer soc esp_lcd)');p.write_text(s)
p=d/'CMakeLists.txt';p.write_text(p.read_text()+'\nforeach(symbol IN ITEMS lv_display_send_event esp_lcd_panel_draw_bitmap)\n target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,--wrap=${symbol}")\nendforeach()\n')
probe=r'''/* Diagnostic only. Counters add overhead; no driver/cache policy modifications. */
#include <inttypes.h>
#include <string.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc.h"
#include "soc/cache_reg.h"
#define NT 48
#define NS 16
#define MASK (CACHE_L1_IBUS0_CNT_ENA|CACHE_L1_IBUS1_CNT_ENA|CACHE_L1_DBUS0_CNT_ENA|CACHE_L1_DBUS1_CNT_ENA)
static const uint32_t regs[8]={CACHE_L1_IBUS0_ACS_HIT_CNT_REG,CACHE_L1_IBUS0_ACS_MISS_CNT_REG,CACHE_L1_IBUS1_ACS_HIT_CNT_REG,CACHE_L1_IBUS1_ACS_MISS_CNT_REG,CACHE_L1_DBUS0_ACS_HIT_CNT_REG,CACHE_L1_DBUS0_ACS_MISS_CNT_REG,CACHE_L1_DBUS1_ACS_HIT_CNT_REG,CACHE_L1_DBUS1_ACS_MISS_CNT_REG};
typedef struct { char name[24]; unsigned id; uint32_t us; } task_delta_t;
typedef struct { const char *name; uint64_t us,cache[8],refresh,empty,render,flush,panel,errors,pixels,refr_us,render_us,flush_us,wait_us; unsigned cores[2],capacity,stride,nt; task_delta_t tasks[NT]; } scene_t;
static scene_t *scenes;
static TaskStatus_t before[NT], after[NT];
static unsigned nb;
static lv_timer_t *sampler;
static lv_display_t *display;
static int current=-1;
static bool measuring,rendered;
static uint32_t prior[8],saved_ctrl;
static int64_t entered,started,refr_start,render_start,flush_start,wait_start;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static void cache_sample(bool add) {
 for(unsigned i=0;i<8;i++){uint32_t now=REG_READ(regs[i]);if(add)scenes[current].cache[i]+=(uint32_t)(now-prior[i]);prior[i]=now;}
}
static void sample(lv_timer_t *unused) {
 (void)unused; if(current<0||current>=NS)return;
 int64_t now=esp_timer_get_time();
 if(!measuring && now-entered>=1000000){
  nb=uxTaskGetSystemState(before,NT,NULL);cache_sample(false);started=esp_timer_get_time();refr_start=render_start=flush_start=wait_start=0;rendered=false;
  portENTER_CRITICAL(&mux);measuring=true;portEXIT_CRITICAL(&mux);
 } else if(measuring)cache_sample(true);
}
static void finish(void){
 if(current<0||current>=NS||!measuring)return;
 portENTER_CRITICAL(&mux);measuring=false;portEXIT_CRITICAL(&mux);
 scene_t *s=&scenes[current];s->us=esp_timer_get_time()-started;cache_sample(true);
 unsigned n=uxTaskGetSystemState(after,NT,NULL);s->nt=n;
 for(unsigned i=0;i<n;i++){
  snprintf(s->tasks[i].name,sizeof(s->tasks[i].name),"%s",after[i].pcTaskName);s->tasks[i].id=after[i].xTaskNumber;
  for(unsigned j=0;j<nb;j++)if(before[j].xTaskNumber==after[i].xTaskNumber){s->tasks[i].us=(uint32_t)(after[i].ulRunTimeCounter-before[j].ulRunTimeCounter);break;}
 }
}
void bench_probe_scene_enter(unsigned id,const char *name){finish();current=id<NS?(int)id:-1;if(current>=0){scenes[current].name=name;entered=esp_timer_get_time();}}
void bench_probe_install(lv_display_t *d){
 scenes=heap_caps_calloc(NS,sizeof(*scenes),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);assert(scenes);display=d;current=-1;measuring=false;
 saved_ctrl=REG_READ(CACHE_L1_CACHE_ACS_CNT_CTRL_REG)&MASK;REG_SET_BIT(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,MASK);
 sampler=lv_timer_create(sample,250,NULL);
}
lv_result_t __real_lv_display_send_event(lv_display_t*,lv_event_code_t,void*);
lv_result_t __wrap_lv_display_send_event(lv_display_t *d,lv_event_code_t code,void *p){
 if(d==display&&measuring&&current>=0){
  scene_t *s=&scenes[current];int64_t now=esp_timer_get_time();
  switch(code){
  case LV_EVENT_REFR_START:refr_start=now;rendered=false;s->cores[xPortGetCoreID()]++;break;
  case LV_EVENT_REFR_READY:s->refresh++;s->empty+=!rendered;if(refr_start)s->refr_us+=now-refr_start;refr_start=0;break;
  case LV_EVENT_RENDER_START:rendered=true;render_start=now;break;
  case LV_EVENT_RENDER_READY:s->render++;if(render_start)s->render_us+=now-render_start;render_start=0;break;
  case LV_EVENT_FLUSH_START:{s->flush++;flush_start=now;lv_area_t *a=p;if(a)s->pixels+=(uint64_t)lv_area_get_width(a)*lv_area_get_height(a);lv_draw_buf_t *b=lv_display_get_buf_active(d);if(b){s->capacity=b->data_size;s->stride=b->header.stride;}break;}
  case LV_EVENT_FLUSH_FINISH:if(flush_start)s->flush_us+=now-flush_start;flush_start=0;break;
  case LV_EVENT_FLUSH_WAIT_START:wait_start=now;break;
  case LV_EVENT_FLUSH_WAIT_FINISH:if(wait_start)s->wait_us+=now-wait_start;wait_start=0;break;
  default:break;
  }
 }
 return __real_lv_display_send_event(d,code,p);
}
esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t,int,int,int,int,const void*);
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t p,int x,int y,int ex,int ey,const void *data){
 esp_err_t e=__real_esp_lcd_panel_draw_bitmap(p,x,y,ex,ey,data);
 portENTER_CRITICAL(&mux);if(measuring&&current>=0){scenes[current].panel+=e==ESP_OK;scenes[current].errors+=e!=ESP_OK;}portEXIT_CRITICAL(&mux);return e;
}
void bench_probe_dump(void){
 finish();lv_timer_delete(sampler);REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,(REG_READ(CACHE_L1_CACHE_ACS_CNT_CTRL_REG)&~MASK)|saved_ctrl);
 for(unsigned i=0;i<NS;i++){scene_t *s=&scenes[i];
  ESP_LOGI("DEEP_PROBE","scene=%u name=\"%s\" us=%"PRIu64" refresh=%"PRIu64" empty=%"PRIu64" render=%"PRIu64" flush=%"PRIu64" panel=%"PRIu64" errors=%"PRIu64" pixels=%"PRIu64" refr_us=%"PRIu64" render_us=%"PRIu64" flush_us=%"PRIu64" wait_us=%"PRIu64" core0=%u core1=%u capacity=%u stride=%u",i,s->name?s->name:"",s->us,s->refresh,s->empty,s->render,s->flush,s->panel,s->errors,s->pixels,s->refr_us,s->render_us,s->flush_us,s->wait_us,s->cores[0],s->cores[1],s->capacity,s->stride);
  ESP_LOGI("DEEP_CACHE","scene=%u ih0=%"PRIu64" im0=%"PRIu64" ih1=%"PRIu64" im1=%"PRIu64" dh0=%"PRIu64" dm0=%"PRIu64" dh1=%"PRIu64" dm1=%"PRIu64,i,s->cache[0],s->cache[1],s->cache[2],s->cache[3],s->cache[4],s->cache[5],s->cache[6],s->cache[7]);
  for(unsigned j=0;j<s->nt;j++)ESP_LOGI("DEEP_TASK","scene=%u id=%u name=\"%s\" us=%"PRIu32,i,s->tasks[j].id,s->tasks[j].name,s->tasks[j].us);
 }
 heap_caps_free(scenes);scenes=NULL;display=NULL;current=-1;
}
'''
(d/'main/probe.c').write_text(probe)
for v,xip in [('off',False),('on',True)]:
 r=R/v;r.mkdir(exist_ok=True);shutil.copytree(R/'template',r/'source',dirs_exist_ok=True)
 p=r/'source/components/display/tools/esp_display_present/dev/boards/configs/s31-rgb.defaults'
 s=p.read_text()+'\nCONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y\nCONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y\n'
 if xip:s+='CONFIG_SPIRAM_XIP_FROM_PSRAM=y\nCONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\nCONFIG_SPIRAM_RODATA=y\n'
 p.write_text(s)
 dep=r/'deps'
 if not dep.exists():dep.symlink_to('/tmp/present-five-board-20261109/rgb-retry/deps',target_is_directory=True)
print(R)
