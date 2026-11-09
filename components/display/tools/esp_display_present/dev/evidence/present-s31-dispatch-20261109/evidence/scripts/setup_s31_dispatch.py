from pathlib import Path
import shutil,subprocess,json,hashlib,difflib
R=Path('/tmp/present-s31-dispatch-20261109');R.mkdir(exist_ok=True)
repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR');S=R/'template';S.mkdir(exist_ok=True)
# Copy only the runtime components plus their maintained dev tree; avoid historical evidence/builds.
for name in ['esp_display_present','esp_lv_present','esp_lvgl_adapter']:
 src=repo/'components/display/tools'/name;dst=S/'components/display/tools'/name
 shutil.copytree(src,dst,dirs_exist_ok=True,ignore=shutil.ignore_patterns('evidence','experimental','__pycache__','build*','udo nmcli*'))
D=S/'components/display/tools/esp_display_present/dev';(R/'lvgl').mkdir(exist_ok=True)
shutil.copytree('/home/xuxin/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.6.0~1_7d824107',R/'lvgl',dirs_exist_ok=True)
p=R/'lvgl/demos/benchmark/lv_demo_benchmark.c';s=p.read_text();anchor='static void load_scene(lv_obj_t * scr, uint32_t scene)\n{';assert s.count(anchor)==1;s=s.replace(anchor,'extern void bench_probe_scene_enter(unsigned scene, const char *name);\n'+anchor+'\n    bench_probe_scene_enter(scene, scenes[scene].name);');p.write_text(s)
p=S/'components/display/tools/esp_display_present/src/pixel/present_pixel_engine.c';before=p.read_text();start=before.index('esp_err_t esp_display_present_pixel_submit(');end=before.index('\nstatic copy_record_t *find_record',start);part=before[start:end]
anchor='    unsigned priority = engine->worker.asynchronous ? present_task_priority() : 0;';assert part.count(anchor)==1
part=part.replace('engine->worker.asynchronous','dispatch_async')
part=part.replace('    unsigned priority = dispatch_async ?', '    /* Diagnostic only: retain the worker/resources; bypass dispatch for this case. */\n    extern bool bench_diag_inline(void);\n    bool dispatch_async = engine->worker.asynchronous && !bench_diag_inline();\n    unsigned priority = dispatch_async ?',1)
after=before[:start]+part+before[end:];p.write_text(after)
(R/'dispatch-only.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/src/pixel/present_pixel_engine.c',tofile='b/src/pixel/present_pixel_engine.c')))
main=D/'test_apps/benchmark/main';p=main/'CMakeLists.txt';s=p.read_text().replace('SRCS main.c buffer_audit.c ${BRIDGE}','SRCS main.c buffer_audit.c probe.c diag.c ${BRIDGE}').replace('hw_init lvgl esp_timer)','hw_init lvgl esp_timer soc esp_lcd)');p.write_text(s)
p=D/'test_apps/benchmark/CMakeLists.txt';p.write_text(p.read_text()+'\nforeach(symbol IN ITEMS lv_display_send_event esp_lcd_panel_draw_bitmap esp_display_present_executor_run)\n target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE "-Wl,--wrap=${symbol}")\nendforeach()\n')
# Same binary: for each angle run W,I,I,W; adapter repeats same case twice.
p=main/'main.c';s=p.read_text().replace('static const char *TAG','void bench_probe_install(lv_display_t *);\nvoid bench_probe_stop(void);\nvoid bench_probe_dump(void);\nvoid bench_diag_mode(bool);\nstatic const char *TAG',1)
s=s.replace('for (unsigned i = 0; i < sizeof(bench_cases) / sizeof(bench_cases[0]); i++) {\n        active = &bench_cases[i];','''const unsigned ncases = sizeof(bench_cases) / sizeof(bench_cases[0]);
#ifdef BENCH_ADAPTER
    const unsigned repeats = 2;
#else
    const unsigned repeats = 4;
#endif
    for (unsigned pass = 0; pass < ncases * repeats; pass++) {
        unsigned index = pass / repeats, repeat = pass % repeats;
        active = &bench_cases[index];
        bool inline_mode = false;
#ifndef BENCH_ADAPTER
        inline_mode = repeat == 1 || repeat == 2;
#endif
        bench_diag_mode(inline_mode);
        ESP_LOGI(TAG, "DIAG_CASE pass=%u repeat=%u execution=%s rotation=%u", pass, repeat,
#ifdef BENCH_ADAPTER
                 "adapter",
#else
                 inline_mode ? "inline" : "worker",
#endif
                 active->rotation);''')
s=s.replace('        bench_buffer_audit_start(disp, active);','        bench_buffer_audit_start(disp, active);\n        bench_probe_install(disp);')
s=s.replace('        bench_buffer_audit_stop(disp);','        bench_buffer_audit_stop(disp);\n        bench_probe_stop();')
s=s.replace('        memory_report("after_close", false);','        memory_report("after_close", false);\n        bench_probe_dump();')
p.write_text(s)
# Probe based on previously validated measurement windows, now with fill/conflict counters.
probe=Path('/tmp/present-s31-deep-analysis-20261109/off/source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main/probe.c').read_text()
buses=['IBUS0','IBUS1','DBUS0','DBUS1'];suffixes=['HIT','MISS','CONFLICT','NXTLVL_RD'];regs=[f'CACHE_L1_{bus}_ACS_{suffix}_CNT_REG' for bus in buses for suffix in suffixes]
start=probe.index('static const uint32_t regs[8]');end=probe.index('\ntypedef struct',start)
probe=probe[:start]+'static const uint32_t regs[16]={'+','.join(regs)+'};'+probe[end:]
probe=probe.replace('cache[8]','cache[16]').replace('i<8','i<16')
probe=probe.replace('unsigned cores[2],capacity,stride,nt;', 'uint64_t exec_us,exec_n,exec_errors,exec_core[2],exec_bytes,exec_hw,exec_cpu; unsigned cores[2],capacity,stride,nt;')
anchor='void bench_probe_dump(void){\n finish();lv_timer_delete(sampler);REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,(REG_READ(CACHE_L1_CACHE_ACS_CNT_CTRL_REG)&~MASK)|saved_ctrl);'
assert anchor in probe
probe=probe.replace(anchor,'''void bench_probe_stop(void){
 finish();lv_timer_delete(sampler);REG_WRITE(CACHE_L1_CACHE_ACS_CNT_CTRL_REG,(REG_READ(CACHE_L1_CACHE_ACS_CNT_CTRL_REG)&~MASK)|saved_ctrl);
}
void bench_probe_execution(uint64_t us,uint64_t bytes,unsigned core,bool hw,bool error){
 portENTER_CRITICAL(&mux);
 if(measuring&&current>=0){scene_t *s=&scenes[current];s->exec_us+=us;s->exec_n++;s->exec_errors+=error;s->exec_core[core]++;s->exec_bytes+=bytes;s->exec_hw+=hw;s->exec_cpu+=!hw;}
 portEXIT_CRITICAL(&mux);
}
void bench_probe_dump(void){''')
start=probe.index('  ESP_LOGI("DEEP_CACHE"');end=probe.index('\n  for(unsigned j=',start)
keys=['ia0','is0','ic0','if0','ia1','is1','ic1','if1','da0','ds0','dc0','df0','da1','ds1','dc1','df1']
fmt=' '.join(k+'=%"PRIu64"' for k in keys)
probe=probe[:start]+'  ESP_LOGI("DEEP_CACHE","scene=%u '+fmt+' saturated=%"PRIu64,i,'+','.join('s->cache['+str(i)+']' for i in range(16))+',s->saturated);\n'+'''  ESP_LOGI("DEEP_EXEC","scene=%u calls=%"PRIu64" us=%"PRIu64" bytes=%"PRIu64" core0=%"PRIu64" core1=%"PRIu64" hw=%"PRIu64" cpu=%"PRIu64" errors=%"PRIu64,i,s->exec_n,s->exec_us,s->exec_bytes,s->exec_core[0],s->exec_core[1],s->exec_hw,s->exec_cpu,s->exec_errors);'''+probe[end:]
(main/'probe.c').write_text(probe)
(main/'diag.c').write_text('''/* Diagnostic only. Switch dispatch at case boundaries; no scanout changes. */
#include "bench_api.h"
#include "pixel/present_executor.h"
#include "esp_timer.h"
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
''')
p=main/'diag.c';p.write_text('#include <string.h>\n'+p.read_text())
rev=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
for variant,xip in [('off',False),('on',True)]:
 r=R/variant;shutil.copytree(S,r/'source',dirs_exist_ok=True)
 p=r/'source/components/display/tools/esp_display_present/dev/boards/configs/s31-rgb.defaults'
 s=p.read_text()+'\nCONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y\nCONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER=y\n'
 if xip:s+='CONFIG_SPIRAM_XIP_FROM_PSRAM=y\nCONFIG_SPIRAM_FETCH_INSTRUCTIONS=y\nCONFIG_SPIRAM_RODATA=y\n'
 p.write_text(s)
 if not (r/'deps').exists():(r/'deps').symlink_to('/tmp/present-five-board-20261109/rgb-retry/deps',target_is_directory=True)
(R/'scope.json').write_text(json.dumps(dict(revision=rev,board='s31-rgb',mac='30:ed:a0:f3:f7:b8',usb_serial='9ebcd36ef471f0119543029f1045c30f',port='/dev/ttyUSB0',profiles=['DOUBLE_PARTIAL'],angles=[0,90],input_tiles=1,capacity_bytes={'0':80000,'90':48000},xip=['off','on'],present_order='per angle: worker,inline,inline,worker; same binary and retained worker resources',adapter_order='per angle: adapter,adapter',idf_patched=False,production_runtime_modified=False),indent=2)+'\n')
print(R)
