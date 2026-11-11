from pathlib import Path
import json,re,hashlib,subprocess,gzip,importlib.util
R=Path(__file__).parent;D=json.loads((R/'matrix-data.json').read_text());assert D['complete']
P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools');IDF=Path('/home/xuxin/esp_work/esp-idf-qspi');frozen=json.loads((R/'frozen-source.json').read_text());expected={(m,a) for m in ['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'] for a in [0,90,180,270]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def hashes(root):return {str(f.relative_to(root)):sha(f) for f in sorted(root.rglob('*')) if f.is_file()}
for comp,sub,key in [('esp_display_present','src','present_src'),('esp_display_present','include','present_include'),('esp_lvgl_adapter','src','adapter_src'),('esp_lv_present','src','lv_present_src')]:
 assert hashes(P/comp/sub)==frozen[key],('Production changed',comp,sub)
 assert hashes(R/'clean-source/components/display/tools'/comp/sub)==frozen[key]
assert subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=IDF,text=True)==''
overlay=R/'driver-overlay';trace=json.loads((overlay/'trace-manifest.json').read_text());assert trace['idf_commit']==subprocess.check_output(['git','rev-parse','HEAD'],cwd=IDF,text=True).strip()
for path,h in trace['files_sha256'].items():assert sha(overlay/path)==h
for rel in ['rgb/esp_lcd_panel_rgb.c','dsi/esp_lcd_panel_dpi.c']:
 original=[s.strip() for s in (IDF/'components/esp_lcd'/rel).read_text().splitlines() if s.strip()];changed=iter(s.strip() for s in (overlay/'esp_lcd'/rel).read_text().splitlines() if s.strip())
 for line in original:assert any(x==line for x in changed),('Control line removed or reordered',rel,line)
for comp,rel in [('esp_display_present','src/backend/present_backend_fb.c'),('esp_lvgl_adapter','src/display/bridge/common/display_bridge_common.c'),('esp_lvgl_adapter','src/display/bridge/v9/lvgl_bridge_v9.c')]:
 s=(R/'diag-source/components/display/tools'/comp/rel).read_text().replace('#include "esp_lcd_present_trace.h"\n','');s=re.sub(r'(?m)^\s*(?:if \(next\) )?esp_lcd_present_trace_audit\([^\n]+\);\n','\n',s)
 assert re.sub(r'\s+','',s)==re.sub(r'\s+','',(P/comp/rel).read_text())
spec=importlib.util.spec_from_file_location('raw_parser',R/'clean-source/components/display/tools/esp_display_present/dev/tools/run.py');parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
ledger=json.loads((R/'p4-dsi/case-ledger.json').read_text())
for cohort,rows in ledger.items():
 if cohort not in ('clean','diagnostic'):continue
 for row in rows:
  src=Path(row['source']);text=gzip.open(src/'live.log.gz','rt',errors='replace').read() if (src/'live.log.gz').exists() else (src/'live.log').read_text(errors='replace');raw=parser.parse_log(text);key=row['mode'],row['rotation']
  if row['status']=='PASS':
   original=next(x for x in raw['rows'] if (x['mode'],x['rotation'])==key)
   assert all(row[k]==v for k,v in original.items()) and 'after_close' in original['memory']
  else:
   assert raw['errors'] and not any((x['mode'],x['rotation'])==key for x in raw['rows'])
   active=re.findall(r'RUN mode=(\w+).*?rotation=(\d+)',text);assert active and (active[-1][0],int(active[-1][1]))==key
counts={'clean':0,'diagnostic':0};failed={'clean':0,'diagnostic':0};hits=[];unknown={};recovered=0
for board,b in D['boards'].items():
 assert b['controller']['success']
 adapter=b['cohorts']['clean']['adapter']['manifest']
 for cohort,pair in b['cohorts'].items():
  for impl,v in pair.items():
   rows=v['rows'];assert len(rows)==24 and {(x['mode'],x['rotation']) for x in rows}==expected
   manifests=v.get('manifests',[v.get('manifest')])
   assert all(manifests)
   for m in manifests:
    assert m['xip_mode']=='on' and m['idf_commit']==trace['idf_commit']
    for key in ['lvgl_benchmark_sha256','comparable_sdkconfig_sha256','rgb_bounce_lines']:
     assert m[key]==adapter[key],(board,cohort,impl,key)
    if m['defaults_sha256']!=adapter['defaults_sha256']:
     assert board=='s3-rgb' and cohort=='diagnostic' and impl=='adapter'
     old=Path('/tmp/present-xip-on-diag-source-20261110/components/display/tools/esp_display_present/dev/boards/configs/s3-rgb.defaults');new=R/'diag-source/components/display/tools/esp_display_present/dev/boards/configs/s3-rgb.defaults'
     assert sha(old)==m['defaults_sha256']['s3-rgb.defaults'] and sha(new)==adapter['defaults_sha256']['s3-rgb.defaults'] and old.read_bytes().rstrip()==new.read_bytes().rstrip()
    assert all(c['tiles']==1 and c['name']!='NONE' for c in m['cases'])
    assert bool(m['driver_trace'])==(cohort=='diagnostic')
   for row in rows:
    if row['status']=='FAIL':
     assert board=='p4-dsi' and (row['mode'],row['rotation']) in expected and row.get('failure_reason');failed[cohort]+=1;continue
    if row['status']=='SKIP':assert impl=='adapter' and row['mode']=='AUTO';continue
    assert row['status']=='PASS' and row['scenes']==16 and len(row['scene_scores'])==16 and len({x['name'] for x in row['scene_scores']})==16
    a=row['buffer_audit'];assert a['elapsed_us']>=60000000 and a['rendered']>0 and a['pixels']>0 and a['flushes']>0 and a['input_tiles']==1
    if row['mode'] in ['DOUBLE_FULL','TRIPLE_FULL']:assert a['active_bytes']==a['logical_w']*a['logical_h']*2
    if row['mode'] in ['DOUBLE_PARTIAL','TRIPLE_PARTIAL']:assert a['active_bytes']==a['logical_w']*50*2
    assert set(row['memory'])=={'before_open','after_open','after_run','after_close'}
    for phase,mem in row['memory'].items():
     for heap in ['internal','psram']:assert 0<mem[heap+'_free']<=mem[heap+'_total']
    for heap in ['internal','psram']:assert 0<row['memory']['after_run'][heap+'_min_free']<=row['memory']['after_open'][heap+'_free']
    if impl=='present' and cohort=='clean':
     assert row['memory']['before_open']['psram_free']==row['memory']['after_close']['psram_free'];recovered+=1
    if cohort=='diagnostic':
     address=row['address'];assert address['writes']>100 and address['boundaries']>1000
     if impl=='present':assert address['releases']>100 and address['confirms']>100
     elif row['mode'] in ['DOUBLE_FULL','DOUBLE_DIRECT'] and row['rotation']==0:assert address['confirms']>100 and address['releases']==0
     else:assert address['releases']>100 and address['confirms']==0
     key=board+'/'+impl+'/'+row['mode']+'/'+str(row['rotation']);unknown[key]=address['raced'];n=sum(address[k] for k in ['release_active','write_active','confirm_wrong'])
     if n:hits.append({'board':board,'implementation':impl,'mode':row['mode'],'angle':row['rotation'],'hits':n,'events':row['address_events']})
     if board=='s3-rgb':assert row['supply']['expected_eofs']==48 and row['supply']['measured_frames']>1000
    counts[cohort]+=1
 # Verify each newly captured build used the intended component, actual FB configuration and XIP flags.
 for traced in [False,True]:
  v=R/board/'benchmark'/('present-xip-on'+('-driver-trace' if traced else ''))
  if board!='p4-dsi':
   result=json.loads((v/'results.json').read_text());assert result['complete'] and result['matrix_complete'] and result['buffers_verified'] and not result['errors'];assert len(result['rows'])==(24 if traced else (4 if board=='s31-rgb' else 16))
  else:
   ledger=json.loads((R/board/'case-ledger.json').read_text());assert len(ledger['diagnostic' if traced else 'clean'])==(24 if traced else 16)
  sdk=(v/'sdkconfig').read_text()
  for flag in ['CONFIG_SPIRAM_XIP_FROM_PSRAM=y','CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y']:assert flag in sdk
  project=json.loads((v/'build/project_description.json').read_text());assert Path(project['build_component_info']['esp_lcd']['dir']).resolve()==(overlay/'esp_lcd' if traced else IDF/'components/esp_lcd').resolve()
  log=(v/'live.log').read_text(errors='replace')
  if board in ['s3-rgb','s31-rgb']:assert f'bounce_bytes={9600 if board=="s3-rgb" else 0} ' in log and ' fbs=3 ' in log
assert counts['clean']+failed['clean']==132 and counts['diagnostic']+failed['diagnostic']==132 and recovered==72-failed['clean']
out={'passed':True,'counts':counts,'supported_scenes':sum(counts.values())*16,'failed_cases':failed,'skips':24,'current_source_checked':D['source_commit'],'reuse_audit':json.loads((R/'reuse-validation.json').read_text()),'new_clean_present':48,'reused_clean_present':24,'reused_adapter':60,'new_diagnostic_present':72,'reused_diagnostic_adapter':60,'present_clean_psram_recovered_cases':recovered,'base_idf_clean':True,'original_driver_control_preserved':True,'nonzero_observer_coverage':True,'address_hits':hits,'unknown_per_case':unknown}
(R/'validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n');print(json.dumps({'passed':True,'counts':counts,'failed':failed,'address_affected_cases':len(hits),'unknown_total':sum(unknown.values())}))
