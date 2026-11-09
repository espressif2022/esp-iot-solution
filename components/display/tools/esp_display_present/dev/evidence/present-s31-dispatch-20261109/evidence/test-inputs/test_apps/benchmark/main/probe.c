/* Diagnostic only. Counters add overhead; no driver/cache policy modifications. */
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
#define CLEAR (CACHE_L1_IBUS0_CNT_CLR|CACHE_L1_IBUS1_CNT_CLR|CACHE_L1_DBUS0_CNT_CLR|CACHE_L1_DBUS1_CNT_CLR)
#define NT 48
#define NS 16
#define MASK (CACHE_L1_IBUS0_CNT_ENA|CACHE_L1_IBUS1_CNT_ENA|CACHE_L1_DBUS0_CNT_ENA|CACHE_L1_DBUS1_CNT_ENA)
static const uint32_t regs[16]={CACHE_L1_IBUS0_ACS_HIT_CNT_REG,CACHE_L1_IBUS0_ACS_MISS_CNT_REG,CACHE_L1_IBUS0_ACS_CONFLICT_CNT_REG,CACHE_L1_IBUS0_ACS_NXTLVL_RD_CNT_REG,CACHE_L1_IBUS1_ACS_HIT_CNT_REG,CACHE_L1_IBUS1_ACS_MISS_CNT_REG,CACHE_L1_IBUS1_ACS_CONFLICT_CNT_REG,CACHE_L1_IBUS1_ACS_NXTLVL_RD_CNT_REG,CACHE_L1_DBUS0_ACS_HIT_CNT_REG,CACHE_L1_DBUS0_ACS_MISS_CNT_REG,CACHE_L1_DBUS0_ACS_CONFLICT_CNT_REG,CACHE_L1_DBUS0_ACS_NXTLVL_RD_CNT_REG,CACHE_L1_DBUS1_ACS_HIT_CNT_REG,CACHE_L1_DBUS1_ACS_MISS_CNT_REG,CACHE_L1_DBUS1_ACS_CONFLICT_CNT_REG,CACHE_L1_DBUS1_ACS_NXTLVL_RD_CNT_REG};
typedef struct { char name[24]; unsigned id; uint32_t us; } task_delta_t;
typedef struct { const char *name; uint64_t us,cache[16],saturated,refresh,empty,render,flush,panel,errors,pixels,refr_us,render_us,flush_us,wait_us; uint64_t exec_us,exec_n,exec_errors,exec_core[2],exec_bytes,exec_hw,exec_cpu; unsigned cores[2],capacity,stride,nt; task_delta_t tasks[NT]; } scene_t;
static scene_t *scenes;
static TaskStatus_t before[NT], after[NT];
static unsigned nb;
static lv_timer_t *sampler;
static lv_display_t *display;
static int current=-1;
static bool measuring,rendered;
static uint32_t saved_ctrl;
static int64_t entered,started,refr_start,render_start,flush_start,wait_start;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static void cache_sample(bool add) {
 for(unsigned i=0;i<16;i++){uint32_t now=REG_READ(regs[i]);if(add){scenes[current].cache[i]+=now;scenes[current].saturated+=now==UINT32_MAX;}}
 REG_SET_BIT(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,CLEAR);
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
void bench_probe_stop(void){
 finish();lv_timer_delete(sampler);REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,(REG_READ(CACHE_L1_CACHE_ACS_CNT_CTRL_REG)&~MASK)|saved_ctrl);
}
void bench_probe_execution(uint64_t us,uint64_t bytes,unsigned core,bool hw,bool error){
 portENTER_CRITICAL(&mux);
 if(measuring&&current>=0){scene_t *s=&scenes[current];s->exec_us+=us;s->exec_n++;s->exec_errors+=error;s->exec_core[core]++;s->exec_bytes+=bytes;s->exec_hw+=hw;s->exec_cpu+=!hw;}
 portEXIT_CRITICAL(&mux);
}
void bench_probe_dump(void){
 for(unsigned i=0;i<NS;i++){scene_t *s=&scenes[i];
  ESP_LOGI("DEEP_PROBE","scene=%u name=\"%s\" us=%"PRIu64" refresh=%"PRIu64" empty=%"PRIu64" render=%"PRIu64" flush=%"PRIu64" panel=%"PRIu64" errors=%"PRIu64" pixels=%"PRIu64" refr_us=%"PRIu64" render_us=%"PRIu64" flush_us=%"PRIu64" wait_us=%"PRIu64" core0=%u core1=%u capacity=%u stride=%u",i,s->name?s->name:"",s->us,s->refresh,s->empty,s->render,s->flush,s->panel,s->errors,s->pixels,s->refr_us,s->render_us,s->flush_us,s->wait_us,s->cores[0],s->cores[1],s->capacity,s->stride);
  ESP_LOGI("DEEP_CACHE","scene=%u ia0=%"PRIu64" is0=%"PRIu64" ic0=%"PRIu64" if0=%"PRIu64" ia1=%"PRIu64" is1=%"PRIu64" ic1=%"PRIu64" if1=%"PRIu64" da0=%"PRIu64" ds0=%"PRIu64" dc0=%"PRIu64" df0=%"PRIu64" da1=%"PRIu64" ds1=%"PRIu64" dc1=%"PRIu64" df1=%"PRIu64" saturated=%"PRIu64,i,s->cache[0],s->cache[1],s->cache[2],s->cache[3],s->cache[4],s->cache[5],s->cache[6],s->cache[7],s->cache[8],s->cache[9],s->cache[10],s->cache[11],s->cache[12],s->cache[13],s->cache[14],s->cache[15],s->saturated);
  ESP_LOGI("DEEP_EXEC","scene=%u calls=%"PRIu64" us=%"PRIu64" bytes=%"PRIu64" core0=%"PRIu64" core1=%"PRIu64" hw=%"PRIu64" cpu=%"PRIu64" errors=%"PRIu64,i,s->exec_n,s->exec_us,s->exec_bytes,s->exec_core[0],s->exec_core[1],s->exec_hw,s->exec_cpu,s->exec_errors);
  for(unsigned j=0;j<s->nt;j++)ESP_LOGI("DEEP_TASK","scene=%u id=%u name=\"%s\" us=%"PRIu32,i,s->tasks[j].id,s->tasks[j].name,s->tasks[j].us);
 }
 heap_caps_free(scenes);scenes=NULL;display=NULL;current=-1;
}
