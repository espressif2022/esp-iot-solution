from pathlib import Path
import json,subprocess,sys,importlib.util,datetime,re,gzip,copy
from zoneinfo import ZoneInfo
R=Path(__file__).parent;P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');E=P/'dev/evidence';S=R/'clean-source/components/display/tools/esp_display_present'
spec=importlib.util.spec_from_file_location('runner_current',S/'dev/tools/run.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
spec=importlib.util.spec_from_file_location('memory_current',S/'dev/tools/collect_memory_accounting.py');accounting=importlib.util.module_from_spec(spec);spec.loader.exec_module(accounting)
MODES=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];ANGLES=[0,90,180,270];BOARDS=['s3-rgb','s31-rgb','p4-dsi'];commit=json.loads((R/'source.json').read_text())['commit'];old=json.loads((E/'rgb-mipi-xip-on-no-fb-iram-20261110/matrix-data.json').read_text());audit=json.loads((R/'reuse-validation.json').read_text())
def extract(path, allow_partial=False):
 path=Path(path);text=gzip.open(path/'live.log.gz','rt',errors='replace').read() if (path/'live.log.gz').exists() else (path/'live.log').read_text(errors='replace');parsed=runner.parse_log(text);scores={};addresses={};supplies={};events=[]
 if allow_partial:
  assert len(parsed['rows'])>0 and all(x['status']=='PASS' and 'after_close' in x.get('memory',{}) for x in parsed['rows'])
  assert not parsed['errors'] or all('ESP_ERROR_CHECK failed' in e for e in parsed['errors'])
 else:
  assert parsed['complete'] and not parsed['errors'],(path,parsed['errors'])
 for line in text.splitlines():
  m=re.search(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',line)
  if m:scores.setdefault((m[1],int(m[2])),[]).append({'name':m[3],'fps':int(m[4])})
  if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
   d=dict(re.findall(r'(\w+)=([^ ]+)',line));d={k:int(v) if v.isdecimal() else v for k,v in d.items()};key=(d['mode'],d['rotation'])
   if line.startswith('RGB_ADDRESS '):addresses[key]=d
   elif line.startswith('RGB_SUPPLY '):supplies[key]=d
   else:events.append(d)
 for row in parsed['rows']:
  key=row['mode'],row['rotation'];row['scene_scores']=scores.get(key,[])
  if addresses:row.update(address=addresses.get(key),supply=supplies.get(key),address_events=[x for x in events if (x['mode'],x['rotation'])==key])
 return parsed
D={'xip':'on','angles':ANGLES,'fresh_full_run':False,'current_round':{'label':'当前源码有效矩阵 · 已测项复用，缺项补齐','source_commit':commit,'reused_present_clean':24,'new_present_clean':48,'reused_adapter_clean':60,'new_present_diagnostic':72,'reused_adapter_diagnostic':60},'source_commit':commit,'updated':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),'boards':{},'summary':{'clean_pass':0,'diagnostic_pass':0,'skip':0,'address_hits':0,'errors':[],'clean_fail':0,'diagnostic_fail':0},'methods':json.loads((R/'diagnostic-source.json').read_text())['methods'],'round_note':'当前源码的六模式、四角度有效成绩；只补缺项。复用已验证源码与配置一致的 present 24 项和 adapter 成绩，新补 present 48 项正式成绩及 72 项独立交接诊断。所有正式成绩合计 132 项，独立诊断 132 项；NONE 排除，adapter 不支持 AUTO 记“−”。不展示失效旧版本成绩。','memory_accounting':{'schema':1,'scope':'whole benchmark firmware; not component-exclusive','boards':{}}}
for board in BOARDS:
 controller=json.loads((R/board/'supplement-controller.json').read_text());assert controller['success']
 b=D['boards'][board]={'cohorts':{},'controller':controller,'config':{'s3-rgb':'XIP ON · 10 行 bounce · 无整组 FB_IRAM 映射','s31-rgb':'XIP ON · 非 bounce · AXI link-switch','p4-dsi':'XIP ON · DW GDMA'}[board]}
 v=R/board/'benchmark/present-xip-on';latest=E/'scanout-lifecycle-cleanup-20261111'/board;a=extract(latest)
 assert len(a['rows'])==8
 ledger=json.loads((R/board/'case-ledger.json').read_text()) if board=='p4-dsi' else None
 if ledger:
  assert len(ledger['clean'])==16
  rows=a['rows']+ledger['clean']
 elif board=='s31-rgb':
  earlier=extract(R/'diagnostics/s31-serial-interruption',True);z=extract(v);assert len(earlier['rows'])==12 and len(z['rows'])==4;rows=a['rows']+earlier['rows']+z['rows']
 else:
  z=extract(v);assert len(z['rows'])==16;rows=a['rows']+z['rows']
 assert len({(x['mode'],x['rotation']) for x in rows})==24
 rows=sorted(rows,key=lambda x:(MODES.index(x['mode']),x['rotation']))
 for cohort in ['clean','diagnostic']:
  b['cohorts'][cohort]={}
  if cohort=='clean':
   sources=([str(latest)]+sorted({x['source'] for x in ledger['clean']})) if ledger else ([str(latest),str(R/'diagnostics/s31-serial-interruption'),str(v)] if board=='s31-rgb' else [str(latest),str(v)])
   pair={'rows':rows,'complete':all(x['status']=='PASS' for x in rows),'coverage_complete':True,'errors':[],'sources':sources,'manifests':[json.loads((Path(path)/'manifest.json').read_text()) for path in sources]}
  else:
   if ledger:
    assert len(ledger['diagnostic'])==24
    dr=sorted(ledger['diagnostic'],key=lambda x:(MODES.index(x['mode']),x['rotation']));sources=sorted({x['source'] for x in dr})
    pair={'rows':dr,'complete':all(x['status']=='PASS' for x in dr),'coverage_complete':True,'errors':[],'sources':sources,'manifests':[json.loads((Path(path)/'manifest.json').read_text()) for path in sources]}
   else:
    vdiag=R/board/'benchmark/present-xip-on-driver-trace';parsed=extract(vdiag);assert len(parsed['rows'])==24;pair={**parsed,'source':str(vdiag),'manifest':json.loads((vdiag/'manifest.json').read_text())}
  b['cohorts'][cohort]['present']=pair
  b['cohorts'][cohort]['adapter']=copy.deepcopy(old['boards'][board]['cohorts'][cohort]['adapter'])
  for impl,vv in b['cohorts'][cohort].items():
   for row in vv['rows']:
    if row['status']=='SKIP':assert impl=='adapter' and row['mode']=='AUTO';D['summary']['skip']+=1;continue
    if row['status']=='FAIL':D['summary'][cohort+'_fail']+=1;continue
    assert row['status']=='PASS' and row['scenes']==16 and len(row['scene_scores'])==16
    D['summary'][cohort+'_pass']+=1
    if cohort=='diagnostic':D['summary']['address_hits']+=sum(row['address'][k] for k in ['release_active','write_active','confirm_wrong'])
 D['memory_accounting']['boards'][board]={'present':accounting.collect(v),'adapter':old['memory_accounting']['boards'][board]['adapter']}
D['complete']=D['summary']['clean_pass']+D['summary']['clean_fail']==132 and D['summary']['diagnostic_pass']+D['summary']['diagnostic_fail']==132 and D['summary']['skip']==24
D['coverage_complete']=D['complete'];D['summary']['errors']=[{'board':board,'cohort':co,'implementation':impl,'mode':row['mode'],'rotation':row['rotation'],'reason':row['failure_reason']} for board,b in D['boards'].items() for co,pair in b['cohorts'].items() for impl,record in pair.items() for row in record['rows'] if row['status']=='FAIL'];
D['current_round']['failed_clean']=D['summary']['clean_fail'];D['current_round']['failed_diagnostic']=D['summary']['diagnostic_fail'];D['current_round']['all_passed']=not (D['summary']['clean_fail']+D['summary']['diagnostic_fail']);D['round_note']+=f" 正式 {D['summary']['clean_pass']} 项通过 / {D['summary']['clean_fail']} 项失败，诊断 {D['summary']['diagnostic_pass']} 项完成 / {D['summary']['diagnostic_fail']} 项未完成。失败项不展示无效 FPS；采集批次保留实际 manifest。";
assert D['complete'];(R/'matrix-data.json').write_text(json.dumps(D,ensure_ascii=False,indent=2)+'\n');(R/'memory-accounting.json').write_text(json.dumps(D['memory_accounting'],indent=2)+'\n');print(D['summary'])
