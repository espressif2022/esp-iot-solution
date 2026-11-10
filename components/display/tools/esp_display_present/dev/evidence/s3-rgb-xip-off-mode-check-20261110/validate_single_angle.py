from pathlib import Path
import json,hashlib,subprocess,shutil
R=Path('/tmp/present-s3-xip-off-all-modes-20261110');C=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');D=C/'dev/evidence/s3-rgb-xip-off-mode-check-20261110';S=Path('/tmp/present-s3-off-matrix-source-20261110/components/display/tools/esp_display_present');IDF=Path('/home/xuxin/esp_work/esp-idf-qspi');delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s3-rgb-xip-off-mode-check-20261110')
st=json.loads((R/'single-angle-controller.json').read_text());assert st['stage']=='done' and st['success'] and st['archive_complete'] and not st.get('reference_flash_error'),st
for line in (D/'SHA256SUMS').read_text().splitlines():
 h,n=line.split('  ',1);assert hashlib.sha256((D/n).read_bytes()).hexdigest()==h,n
subprocess.run(['python',str(R/'report_single_angle.py')],check=True);d=json.loads((R/'single-angle-data.json').read_text());assert d['complete'] and d['passed']==11 and d['skipped']==1 and d['selected_scenes']==176
assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=IDF,text=True).strip()
assert not subprocess.check_output(['git','diff','--','src','include'],cwd=C,text=True).strip()
o=json.loads((R/'driver-overlay/trace-manifest.json').read_text())
for n,h in o['files_sha256'].items():assert hashlib.sha256((R/'driver-overlay'/n).read_bytes()).hexdigest()==h,n
paths=[R/'s3-rgb/benchmark/present-xip-off-driver-trace',R/'adapter-prior-angle-data',R/'s3-rgb/benchmark/adapter-xip-off-driver-trace'];ms=[];cfgs=[]
for p in paths:
 m=json.loads((p/'manifest.json').read_text());ms.append(m);assert m['xip_mode']=='off' and m['rgb_bounce_lines']==10 and m['driver_trace']==o
 for n,h in m['test_sources_sha256'].items():assert hashlib.sha256((S/'dev'/n).read_bytes()).hexdigest()==h,n
 text=(p/'live.log').read_text(errors='replace');assert 'requested_pclk=16000000 bounce_lines=10' in text and 'bounce_bytes=9600 ' in text
 cfg=dict(x.split('=',1) for x in (p/'sdkconfig').read_text().splitlines() if x.startswith('CONFIG_') and '=' in x);cfgs.append(cfg)
 for n in ['CONFIG_SPIRAM_XIP_FROM_PSRAM','CONFIG_SPIRAM_FETCH_INSTRUCTIONS','CONFIG_SPIRAM_RODATA','CONFIG_ESP_MM_CACHE_MSYNC_C2M_CHUNKED_OPS']:assert cfg.get(n,'n')=='n'
 assert cfg['CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ']=='240' and cfg['CONFIG_SPIRAM_SPEED']=='80'
assert ms[0]['test_sources_sha256']==ms[1]['test_sources_sha256']==ms[2]['test_sources_sha256'];assert ms[1]['component_sources_sha256']==ms[2]['component_sources_sha256']
prefix=('CONFIG_LV_','CONFIG_SPIRAM','CONFIG_ESP_DEFAULT_CPU_','CONFIG_COMPILER_OPTIMIZATION','CONFIG_FREERTOS_HZ','CONFIG_ESP32','CONFIG_CACHE_','CONFIG_LCD_')
for cfg in cfgs[1:]:
 keys={k for k in cfg.keys()|cfgs[0].keys() if k.startswith(prefix)};assert not {k for k in keys if cfg.get(k,'n')!=cfgs[0].get(k,'n')}
proof={'verified':True,'angle':0,'supported_cases':11,'unsupported_cases':1,'selected_scenes':176,'affected_cases':d['affected'],'summaries':d['summaries'],'common_config_equal':True,'reused_and_new_adapter_runtime_equal':True,'test_source_equal_across_cohorts':True,'driver_overlay_hashes_verified':True,'leading_partial_frame_excluded':True,'production_runtime_unchanged':True,'idf_clean':True,'original_firmware_restored':False,'retained_firmware':st['retained_firmware']}
(D/'validation.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2)+'\n')
for src,dst in [('single-angle-controller.json','controller.json'),('single-angle-data.json','matrix-data.json'),('single-angle.csv','comparison.csv'),('single-angle-comparison.html','comparison.html')]:shutil.copy2(R/src,D/dst)
shutil.copy2(R/'validate_single_angle.py',D/'validate_single_angle.py');(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'));shutil.copytree(D,delivery,dirs_exist_ok=True)
for line in (D/'SHA256SUMS').read_text().splitlines():
 h,n=line.split('  ',1);assert hashlib.sha256((D/n).read_bytes()).hexdigest()==h;assert hashlib.sha256((delivery/n).read_bytes()).hexdigest()==h
print(json.dumps(proof,ensure_ascii=False))
