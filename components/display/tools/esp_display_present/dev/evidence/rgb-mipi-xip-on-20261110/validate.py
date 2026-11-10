from pathlib import Path
import json,re,hashlib,subprocess,difflib
R=Path('/tmp/present-xip-on-comparison-20261110');D=json.loads((R/'matrix-data.json').read_text());assert D['complete'],D['summary'];assert not D['summary']['errors']
prod=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools');old=json.loads((R/'frozen-source.json').read_text());idf=Path('/home/xuxin/esp_work/esp-idf-qspi')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def hashes(root):return {str(x.relative_to(root)):sha(x) for x in sorted(root.rglob('*')) if x.is_file()}
for comp,sub,key in [('esp_display_present','src','present_src'),('esp_display_present','include','present_include'),('esp_lvgl_adapter','src','adapter_src'),('esp_lv_present','src','lv_present_src')]:assert hashes(prod/comp/sub)==old[key],key
assert subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=idf,text=True)==old['idf_status']==''
o=R/'driver-overlay';m=json.loads((o/'trace-manifest.json').read_text())
assert m['idf_commit']==subprocess.check_output(['git','rev-parse','HEAD'],cwd=idf,text=True).strip()
for rel,s in m['files_sha256'].items():assert sha(o/rel)==s,rel
# Original driver control statements remain an ordered subsequence: hooks only.
for rel in ['rgb/esp_lcd_panel_rgb.c','dsi/esp_lcd_panel_dpi.c']:
 base=[x.strip() for x in (idf/'components/esp_lcd'/rel).read_text().splitlines() if x.strip()];new=[x.strip() for x in (o/'esp_lcd'/rel).read_text().splitlines() if x.strip()];it=iter(new)
 for line in base:assert any(x==line for x in it),('Original driver line removed/reordered',rel,line)
# Consumer hooks must strip back to unchanged production control flow.
c=Path('/tmp/present-xip-on-diag-source-20261110/components/display/tools')
for comp,rel in [('esp_display_present','src/backend/present_backend_fb.c'),('esp_lvgl_adapter','src/display/bridge/common/display_bridge_common.c'),('esp_lvgl_adapter','src/display/bridge/v9/lvgl_bridge_v9.c')]:
 s=(c/comp/rel).read_text().replace('#include "esp_lcd_present_trace.h"\n','');s=re.sub(r'(?m)^\s*(?:if \(next\) )?esp_lcd_present_trace_audit\([^\n]+\);\n','\n',s)
 assert re.sub(r'\s+','',s)==re.sub(r'\s+','',(prod/comp/rel).read_text()),(comp,rel)
prefixes=('CONFIG_LV_','CONFIG_SPIRAM','CONFIG_ESP_DEFAULT_CPU_','CONFIG_COMPILER_OPTIMIZATION','CONFIG_FREERTOS_HZ','CONFIG_ESP32','CONFIG_CACHE_','CONFIG_LCD_')
def common(s):return sorted(x for x in s.splitlines() if x.removeprefix('# ').startswith(prefixes))
counts={'clean':0,'diagnostic':0};unknown={};hits=[]
for board,b in D['boards'].items():
 assert b['controller']['success'] and b['controller']['stage']=='done'
 sdk_by_cohort={}
 for cohort,pair in b['cohorts'].items():
  configs={};manifests={}
  for impl,v in pair.items():
   root=Path(v['source']);r=json.loads((root/'results.json').read_text());assert r['complete'] and r['matrix_complete'] and r['buffers_verified'] and not r['errors'];assert len(r['rows'])==6
   manifest=json.loads((root/'manifest.json').read_text());manifests[impl]=manifest;sdk=(root/'sdkconfig').read_text();configs[impl]=common(sdk)
   for flag in ['CONFIG_SPIRAM_XIP_FROM_PSRAM=y','CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y']:assert flag in sdk,(board,cohort,impl,flag)
   assert manifest['xip_mode']=='on' and all(x['rotation']==0 and x['name']!='NONE' and x['tiles']==1 for x in manifest['cases'])
   assert bool(manifest['driver_trace'])==(cohort=='diagnostic')
   project=json.loads((root/'build/project_description.json').read_text());actual=Path(project['build_component_info']['esp_lcd']['dir']).resolve();assert actual==(o/'esp_lcd' if cohort=='diagnostic' else idf/'components/esp_lcd').resolve()
   text=(root/'live.log').read_text(errors='replace')
   if board in ['s3-rgb','s31-rgb']:assert f'bounce_bytes={9600 if board=="s3-rgb" else 0} ' in text and ' fbs=3 ' in text
   for x in v['rows']:
    if x['status']=='SKIP':assert impl=='adapter' and x['mode']=='AUTO';continue
    assert x['status']=='PASS' and x['scenes']==16 and len(x['scene_scores'])==16 and len({s['name'] for s in x['scene_scores']})==16
    a=x['buffer_audit'];assert a['elapsed_us']>=60000000 and a['rendered']>0 and a['flushes']>0 and a['pixels']>0 and a['input_tiles']==1
    if x['mode'] in ['DOUBLE_FULL','TRIPLE_FULL']:assert a['active_bytes']==a['logical_w']*a['logical_h']*2
    if x['mode'] in ['DOUBLE_PARTIAL','TRIPLE_PARTIAL']:assert a['active_bytes']==a['logical_w']*50*2
    assert set(x['memory'])=={'before_open','after_open','after_run','after_close'}
    for phase,vv in x['memory'].items():
     for heap in ['internal','psram']:assert 0<vv[heap+'_free']<=vv[heap+'_total']
    for heap in ['internal','psram']:assert 0<x['memory']['after_run'][heap+'_min_free']<=x['memory']['after_open'][heap+'_free']
    if cohort=='diagnostic':
     addr=x['address'];assert addr and addr['writes']>100 and addr['boundaries']>1000,('Missing observer coverage',board,impl,x['mode'],addr)
     if impl=='present':assert addr['releases']>100 and addr['confirms']>100
     elif x['mode'] in ['DOUBLE_FULL','DOUBLE_DIRECT']:assert addr['confirms']>100
     else:assert addr['releases']>100
     key=board+'/'+impl+'/'+x['mode'];unknown[key]=addr['raced'];n=sum(addr[k] for k in ['release_active','write_active','confirm_wrong'])
     if n:hits.append({'board':board,'implementation':impl,'mode':x['mode'],'hits':n,'events':x['address_events']})
     if board=='s3-rgb':assert x['supply']['expected_eofs']==48 and x['supply']['measured_frames']>1000
    counts[cohort]+=1
  assert configs['present']==configs['adapter'],(board,cohort,'common SDK differs',list(difflib.unified_diff(configs['present'],configs['adapter'])))
  for key in ['board','buffer_policy','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','test_sources_sha256','defaults_sha256','driver_trace','rgb_bounce_lines','xip_mode']:assert manifests['present'][key]==manifests['adapter'][key],(board,cohort,key)
  sdk_by_cohort[cohort]=configs['present']
 # The observer's build option can add platform-specific trace config; clocks/XIP/cache remain identical.
 diff=set(sdk_by_cohort['clean'])^set(sdk_by_cohort['diagnostic']);assert not diff,(board,'cohort config differs',sorted(diff))
assert counts=={'clean':33,'diagnostic':33}
browser=json.loads((R/'browser-validation.json').read_text());assert browser['passed'] and browser['final'] and browser['checkedCells']==330 and browser['memoryArrowsChecked']==120,browser
result={'passed':True,'counts':counts,'skip':6,'scenes':1056,'angles':[0],'xip':'on','source_unchanged':True,'base_idf_clean':True,'no_control_statements_removed':True,'common_config_identical':True,'correct_driver_paths':True,'nonzero_observer_coverage':True,'unknown_per_case':unknown,'address_hits':hits,'browser':browser}
(R/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps({'validation':'PASS','counts':counts,'address_affected_cases':len(hits),'unknown_total':sum(unknown.values())},ensure_ascii=False))
