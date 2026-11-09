from pathlib import Path
import shutil
R=Path('/tmp/present-s31-deep-analysis-20261109');a=R/'attempt1';a.mkdir(exist_ok=True)
shutil.copytree(R/'off/source',a/'source',dirs_exist_ok=True)
for n in ['failure.txt','controller.log','events.jsonl','stage.json']:shutil.copy2(R/n,a/n)
d=a/'present';d.mkdir(exist_ok=True)
for n in ['live.log','manifest.json','sdkconfig','bench_cases.h','build.log']:
 p=R/'off/s31-rgb/benchmark/present'/n
 if p.exists():shutil.copy2(p,d/n)
for v in ['off','on']:
 d=R/v/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main'
 p=d/'probe.c';s=p.read_text();s=s.replace('#define NT 48','#define CLEAR (CACHE_L1_IBUS0_CNT_CLR|CACHE_L1_IBUS1_CNT_CLR|CACHE_L1_DBUS0_CNT_CLR|CACHE_L1_DBUS1_CNT_CLR)\n#define NT 48')
 s=s.replace('uint64_t us,cache[8],refresh','uint64_t us,cache[8],saturated,refresh')
 s=s.replace('static uint32_t prior[8],saved_ctrl;','static uint32_t saved_ctrl;')
 s=s.replace('for(unsigned i=0;i<8;i++){uint32_t now=REG_READ(regs[i]);if(add)scenes[current].cache[i]+=(uint32_t)(now-prior[i]);prior[i]=now;}','for(unsigned i=0;i<8;i++){uint32_t now=REG_READ(regs[i]);if(add){scenes[current].cache[i]+=now;scenes[current].saturated+=now==UINT32_MAX;}}\n REG_SET_BIT(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,CLEAR);')
 s=s.replace('dm1=%"PRIu64,i,','dm1=%"PRIu64" saturated=%"PRIu64,i,').replace('s->cache[6],s->cache[7]);','s->cache[6],s->cache[7],s->saturated);')
 p.write_text(s)
 p=d/'main.c';s=p.read_text();s=s.replace('        ESP_ERROR_CHECK(bench_lock());\n        bench_probe_dump();\n        bench_unlock();\n','')
 s=s.replace('        ESP_ERROR_CHECK(bench_close());','        ESP_ERROR_CHECK(bench_close());\n        bench_probe_dump();');p.write_text(s)
 p=d/'bridge_present.c';s=p.read_text();s=s.replace('#include "bench_api.h"','#include "bench_api.h"\n#include "esp_log.h"')
 s=s.replace('    if (e != ESP_OK) {\n        return e;\n    }\n    /* A binding error', '    ESP_LOGI("DEEP_CLOSE", "quiesce=%s", esp_err_to_name(e));\n    if (e != ESP_OK) {\n        return e;\n    }\n    /* A binding error')
 s=s.replace('    e = esp_lv_present_stop();','    ESP_LOGI("DEEP_CLOSE", "draw_error=%s", esp_err_to_name(draw_error));\n    e = esp_lv_present_stop();\n    ESP_LOGI("DEEP_CLOSE", "stop=%s", esp_err_to_name(e));')
 s=s.replace('    e = esp_display_presenter_delete(presenter);','    e = esp_display_presenter_delete(presenter);\n    ESP_LOGI("DEEP_CLOSE", "delete=%s", esp_err_to_name(e));');p.write_text(s)
 p=R/v/'source/components/display/tools/esp_lv_present/src/present_lvgl.c';s=p.read_text().replace('#include <limits.h>','#include <limits.h>\n#include "esp_log.h"');s=s.replace('        binding->error = error;','        ESP_LOGE("DEEP_BINDING", "fail_refresh error=%s refreshing=%u need_tile=%u", esp_err_to_name(error), binding->refreshing, binding->need_tile);\n        binding->error = error;',1);p.write_text(s)
print('Instrumented copies updated, production untouched')
