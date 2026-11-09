from pathlib import Path
R=Path('/tmp/present-s31-dispatch-20261109')
for base in ['template','off/source','on/source']:
 d=R/base/'components/display/tools/esp_display_present/dev/test_apps/benchmark';p=d/'CMakeLists.txt';s=p.read_text().replace('esp_display_present_executor_run)','esp_display_present_executor_dispatch_execute)');p.write_text(s)
 p=d/'main/diag.c';s=p.read_text();start=s.index('esp_err_t __real_esp_display_present_executor_run');s=s[:start]+'''esp_err_t __wrap_esp_display_present_executor_dispatch_execute(void *d,const esp_display_present_copy_request_t*r){
 esp_display_present_executor_selection_t selection={0};
 uint64_t start=esp_timer_get_time();esp_err_t ret=esp_display_present_executor_run(d,r,&selection);uint64_t elapsed=esp_timer_get_time()-start;
 esp_display_present_executor_caps_t caps={0};bool hw=false;
 if(selection.executor&&esp_display_present_executor_get_caps(selection.executor,&caps)==ESP_OK)hw=caps.name&&strcmp(caps.name,"cpu")!=0;
 bench_probe_execution(elapsed,(uint64_t)r->src_area.width*r->src_area.height*2,xPortGetCoreID(),hw,ret!=ESP_OK);
 return ret;
}
#endif
''';p.write_text(s)
