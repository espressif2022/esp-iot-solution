/* Diagnostic only: all new code/strings in internal RAM, original probe unchanged. */
#include <inttypes.h>
#include <stdlib.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_freertos_hooks.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pc_address.h"
#define BINS 16384
static DRAM_ATTR uint16_t (*hist)[BINS];
static DRAM_ATTR uint32_t samples[2],outside[2],overflow[2],state[2];
static DRAM_ATTR char tag[]="PC_SAMPLE";
static DRAM_ATTR char task_name[]="pc_dump";
static DRAM_ATTR char fmt_sample[]="PC_SAMPLE: core=%u samples=%"PRIu32" outside=%"PRIu32" overflow=%"PRIu32" state=%"PRIu32"\n";
static DRAM_ATTR char fmt_bin[]="PC_BIN: core=%u pc=%08x count=%u\n";
static DRAM_ATTR char fmt_done[]="PC_SAMPLE: PC_DONE\n";
static void IRAM_ATTR tick(void){
 uint32_t pc;__asm__ volatile("csrr %0, mepc":"=r"(pc));unsigned c=xPortGetCoreID();
 if(!PC_MEASURING_ADDR){return;}
 bool on=*(volatile bool*)(uintptr_t)PC_MEASURING_ADDR;
 if(!state[c]){if(!on){return;}state[c]=1;}
 if(state[c]!=1){return;}
 if(!on){state[c]=2;return;}
 samples[c]++;
 if(pc>=0x40000000u&&pc<0x40100000u){unsigned i=(pc-0x40000000u)>>6;if(hist[c][i]!=UINT16_MAX){hist[c][i]++;}else{overflow[c]++;}}
 else{outside[c]++;}
}
static void IRAM_ATTR dump_task(void *arg){
 (void)arg;vTaskDelay(pdMS_TO_TICKS(44000));
 for(unsigned c=0;c<2;c++){
  esp_log(ESP_LOG_CONFIG_INIT(ESP_LOG_INFO),tag,fmt_sample,c,samples[c],outside[c],overflow[c],state[c]);
  unsigned printed=0;
  for(unsigned i=0;i<BINS;i++){
   if(hist[c][i]){esp_log(ESP_LOG_CONFIG_INIT(ESP_LOG_INFO),tag,fmt_bin,c,0x40000000u+(i<<6),hist[c][i]);if(++printed%32==0){vTaskDelay(1);}}
  }
 }
 esp_log(ESP_LOG_CONFIG_INIT(ESP_LOG_INFO),tag,fmt_done);vTaskDelete(NULL);
}
void IRAM_ATTR __attribute__((constructor)) pc_sampler_install(void){
 hist=heap_caps_calloc(2,sizeof(*hist),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!hist){abort();}
 if(esp_register_freertos_tick_hook_for_cpu(tick,0)!=ESP_OK){abort();}
 if(esp_register_freertos_tick_hook_for_cpu(tick,1)!=ESP_OK){abort();}
 if(xTaskCreate(dump_task,task_name,4096,NULL,1,NULL)!=pdPASS){abort();}
}
