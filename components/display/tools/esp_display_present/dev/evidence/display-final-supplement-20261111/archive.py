"""Freeze successful supplemental runs; refuses incomplete cases and never touches serial."""
from pathlib import Path
import json,re,gzip,hashlib,shutil,subprocess,importlib.util
r=Path(__file__).parent;repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR');dev=repo/'components/display/tools/esp_display_present/dev'
e=r/'archive';e.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('account',dev/'tools/collect_memory_accounting.py');account=importlib.util.module_from_spec(spec);spec.loader.exec_module(account)
accounting=json.loads((dev/'evidence/display-latest-20261111/memory-accounting.json').read_text());runs=[]
for b in ['p4-dsi','s31-qspi','box3-spi']:
 state=json.loads((r/b/'status.json').read_text());assert state['success'],state
 status_dest=e/'runs'/b;status_dest.mkdir(parents=True,exist_ok=True);shutil.copyfile(r/b/'status.json',status_dest/'controller-status.json')
 for entry in state['runs']:
  variant=entry['variant'];v=r/b/'benchmark'/variant;cohort='diagnostic' if 'driver-trace' in variant else 'clean';impl='adapter' if b=='s31-qspi' else 'present';data=json.loads((v/'results.json').read_text());text=(v/'live.log').read_text(errors='replace');assert data['complete'] and data['matrix_complete'] and not data['errors']
  original_text=text
  if b=='s31-qspi' and text.count('SUITE_BEGIN ')>1:
   # Exclude only interrupted startup attempts preceding the last full boot.
   cut=text.rfind('ESP-ROM:');assert cut>=0 and 'PASS mode=' not in text[:cut]
   text=text[cut:]
  assert text.count('SUITE_BEGIN ')==1

  scores={};addresses={};supplies={};events=[]
  for line in text.splitlines():
   x=re.search(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',line)
   if x:scores.setdefault((x[1],int(x[2])),[]).append({'name':x[3],'fps':int(x[4])})
   if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
    obs=dict(re.findall(r'(\w+)=([^ ]+)',line));obs={k:int(v) if v.isdecimal() else v for k,v in obs.items()};key=obs['mode'],obs['rotation']
    if line.startswith('RGB_ADDRESS '):addresses[key]=obs
    elif line.startswith('RGB_SUPPLY '):supplies[key]=obs
    else:events.append(obs)
  for row in data['rows']:
   k=row['mode'],row['rotation'];assert row['status']=='PASS' and row['scenes']==16 and len(scores[k])==16 and 'after_close' in row['memory'];row['scene_scores']=scores[k]
   row.update(origin='11-11 · 修复后独立驱动观察' if cohort=='diagnostic' else '11-11 · 顶部标注对照' if b=='s31-qspi' else '11-11 · 当前版本完整补测',source_commit='39156930d',condition='isolated driver observer; separate from performance' if cohort=='diagnostic' else 'clean IDF; matching top title overlay' if b=='s31-qspi' else 'clean IDF; no title overlay')
   if cohort=='diagnostic':
    assert k in addresses and row.get('driver_trace',{}).get('events',0)>0,(k,'missing diagnostic coverage');row.update(address=addresses[k],supply=supplies.get(k),address_events=[x for x in events if (x['mode'],x['rotation'])==k])
   if impl=='adapter':row.pop('transport_fps',None)
  runs.append({'board':b,'cohort':cohort,'implementation':impl,'variant':variant,'rows':data['rows']})
  dest=e/'runs'/b/variant;dest.mkdir(parents=True,exist_ok=True)
  for name in ['manifest.json','sdkconfig','bench_cases.h','variant.defaults','results.json']:
   if (v/name).exists():shutil.copyfile(v/name,dest/name)
  for name in ['build.log']:
   if (v/name).exists():(dest/(name+'.gz')).write_bytes(gzip.compress((v/name).read_bytes(),mtime=0))
  (dest/'live.log.gz').write_bytes(gzip.compress(text.encode(),mtime=0))
  if original_text!=text:
   (dest/'live-with-interrupted-starts.log.gz').write_bytes(gzip.compress((v/'live.log').read_bytes(),mtime=0))
  if cohort=='clean':
   account_root=v
   if original_text!=text:
    account_root=r/'accounting-normalized'/b;account_root.mkdir(parents=True,exist_ok=True)
    if not (account_root/'build').exists():(account_root/'build').symlink_to(v/'build',target_is_directory=True)
    (account_root/'live.log').write_text(text)
   accounting['boards'][b]['accounts'][impl]=account.collect(account_root);accounting['boards'][b]['note']='普通性能固件ELF与公共启动堆实测；整套固件账目，不是组件独占。新旧复用项逐项注明来源。'
for name in ['source.json','runner.py','controller.py','progress.py','hardware-policy.json','processes.json','archive.py','validate_reuse.py','reuse-validation.json']:shutil.copyfile(r/name,e/name)
for p in r.glob('*-link-events.jsonl'):shutil.copyfile(p,e/p.name)
for d in (r/'s31-qspi').glob('usb-interruption-*'):
 dst=e/'interrupted'/d.name;dst.mkdir(parents=True,exist_ok=True)
 for p in d.iterdir():
  if p.suffix=='.log':(dst/(p.name+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
  else:shutil.copyfile(p,dst/p.name)

shutil.copyfile(r/'te-source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main/main.c',e/'te-main-with-title.c')
shutil.copyfile('/tmp/present-current-full-20261111/consumer-observer.patch',e/'consumer-observer.patch')
shutil.copyfile(r/'driver-overlay/trace-manifest.json',e/'driver-trace-manifest.json')
(e/'results.json').write_text(json.dumps({'runs':runs,'restore_original':False},ensure_ascii=False,indent=2)+'\n')
(e/'memory-accounting.json').write_text(json.dumps(accounting,ensure_ascii=False,indent=2)+'\n')
assert sum(len(x['rows']) for x in runs if x['cohort']=='clean')==48
assert sum(len(x['rows']) for x in runs if x['cohort']=='diagnostic')==24
(e/'validation.json').write_text(json.dumps({'clean_cases':48,'diagnostic_cases':24,'scenes':72*16,'all_closed':True,'production_modified':False,'base_idf_clean':not bool(subprocess.check_output(['git','-C','/home/xuxin/esp_work/esp-idf-qspi','status','--porcelain','--untracked-files=no'],text=True).strip()),'restored_original':False},indent=2)+'\n')
print('Archive ready:',e)
