from pathlib import Path
import importlib.util,json,hashlib
repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR');dev=repo/'components/display/tools/esp_display_present/dev';r=Path(__file__).parent;sp=importlib.util.spec_from_file_location('buffers',dev/'tools/benchmark_buffers.py');mod=importlib.util.module_from_spec(sp);sp.loader.exec_module(mod)
old=json.loads(Path('/tmp/present-aligned-fill-20261109/box3-spi/benchmark/adapter/manifest.json').read_text());checked=[]
for name,wanted in old['component_sources_sha256'].items():
 if not name.startswith('esp_lvgl_adapter/'):continue
 data=(repo/'components/display/tools'/name).read_bytes()
 if name.endswith('/display_manager.c'):data=mod.align_adapter_allocation(data.decode()).encode()
 assert hashlib.sha256(data).hexdigest()==wanted,name;checked.append(name)
new=json.loads((r/'box3-spi/benchmark/present/manifest.json').read_text())
for k in ['idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256']:assert old[k]==new[k],k
fix=json.loads((dev/'evidence/p4-rotated-ppa-stall-20261111/regression/manifest.json').read_text());p4=json.loads((r/'p4-dsi/benchmark/present-xip-on/manifest.json').read_text())
for k in ['buffer_policy','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','component_sources_sha256']:assert fix[k]==p4[k],k
out={'spi_adapter_runtime_files_verified':len(checked),'spi_adapter_allocation_overlay_verified':True,'spi_shared_benchmark_config_verified':True,'p4_prior_5_and_new_19_runtime_and_config_match':True,'p4_prior_cases':len(fix['cases']),'p4_supplement_cases':len(p4['cases']),'reused_components':checked}
(r/'reuse-validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n');print({k:v for k,v in out.items() if k!='reused_components'})
