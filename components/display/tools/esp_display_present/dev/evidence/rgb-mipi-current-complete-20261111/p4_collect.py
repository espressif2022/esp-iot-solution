"""Collect every missing case once; preserve completed cases and explicit failures across resets."""
from pathlib import Path
import json,subprocess,sys,time,re,shutil,gzip,traceback,importlib.util
R=Path(__file__).parent;B=R/'p4-dsi';LEDGER=B/'case-ledger.json';RUNNER=R/'missing_runner.py';PORT='/dev/ttyACM0'
def parse(path):
 spec=importlib.util.spec_from_file_location('parse_latest',R/'clean-source/components/display/tools/esp_display_present/dev/tools/run.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);text=(path/'live.log').read_text(errors='replace') if (path/'live.log').exists() else gzip.open(path/'live.log.gz','rt',errors='replace').read();d=m.parse_log(text);scores={};addresses={};supplies={};events=[]
 for line in text.splitlines():
  x=re.search(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',line)
  if x:scores.setdefault((x[1],int(x[2])),[]).append({'name':x[3],'fps':int(x[4])})
  if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
   e=dict(re.findall(r'(\w+)=([^ ]+)',line));e={k:int(v) if v.isdecimal() else v for k,v in e.items()};key=e['mode'],e['rotation']
   if line.startswith('RGB_ADDRESS '):addresses[key]=e
   elif line.startswith('RGB_SUPPLY '):supplies[key]=e
   else:events.append(e)
 for row in d['rows']:
  k=row['mode'],row['rotation'];assert row['status']=='PASS' and row['scenes']==16 and len(scores[k])==16 and 'after_close' in row.get('memory',{});row['scene_scores']=scores[k]
  if addresses:row.update(address=addresses[k],supply=supplies.get(k),address_events=[e for e in events if (e['mode'],e['rotation'])==k])
 return d,text

def save(ledger):LEDGER.write_text(json.dumps(ledger,indent=2)+'\n')
def snapshot(src,dst):
 dst.mkdir(parents=True,exist_ok=True)
 for n in ['manifest.json','sdkconfig','bench_cases.h','variant.defaults']:
  if (src/n).exists():shutil.copyfile(src/n,dst/n)
 for n in ['live.log','build.log']:
  if (src/n).exists():(dst/(n+'.gz')).write_bytes(gzip.compress((src/n).read_bytes(),mtime=0))

def append(ledger,cohort,path,reason=None,key=None):
 d,text=parse(path);items=ledger[cohort];seen={(x['mode'],x['rotation']) for x in items}
 for row in d['rows']:
  k=row['mode'],row['rotation'];assert k not in seen;row['source']=str(path);items.append(row);seen.add(k)
 if reason:
  if key is None:
   keys=re.findall(r'RUN mode=(\w+).*?rotation=(\d+)',text);assert keys;key=keys[-1][0],int(keys[-1][1])
  assert key not in seen
  items.append({'mode':key[0],'rotation':key[1],'status':'FAIL','failure_reason':reason,'scene_scores':[],'source':str(path)})
 save(ledger)

if not LEDGER.exists():
 ledger={'clean':[],'diagnostic':[],'user_scope':'Reuse existing current results; collect missing cases only','production_modified':False}
 append(ledger,'clean',R/'diagnostics/p4-direct270-close-timeout','bench_close ESP_ERR_TIMEOUT; no valid FPS',('DOUBLE_DIRECT',270))
 src=B/'benchmark/present-xip-on';dst=R/'p4-attempts/clean-002';snapshot(src,dst);append(ledger,'clean',dst,'bench_close ESP_ERR_INVALID_STATE; drawing stopped; no valid FPS',('DOUBLE_PARTIAL',90))
else:ledger=json.loads(LEDGER.read_text())
state={'board':'p4-dsi','port':PORT,'success':False,'restore_original':False,'case_granularity':True}
def stage(name,**kwargs):
 state.update(stage=name,updated=time.time(),**kwargs);(B/'supplement-controller.json').write_text(json.dumps(state,indent=2)+'\n');print(name,kwargs,flush=True)
try:
 for cohort,trace in [('clean',False),('diagnostic',True)]:
  number=3 if not trace else 1
  while len(ledger[cohort])<(16 if not trace else 24):
   variant='present-xip-on'+('-driver-trace' if trace else '');root=B/'benchmark'/variant;dest=R/'p4-attempts'/f'{cohort}-{number:03d}';number+=1
   cmd=[sys.executable,str(RUNNER),'ACTION','--board','p4-dsi','--implementation','present','--xip','on','--work-root',str(R),'--port',PORT,'--timeout','4400']
   if trace:cmd+=['--driver-trace',str(R/'driver-overlay')]
   capture_ok=False
   for action in ['build','flash','capture']:
    stage(action,variant=variant,cohort=cohort,covered=len(ledger[cohort]))
    actual=[action if x=='ACTION' else x for x in cmd]
    with (B/f'{dest.name}-{action}.log').open('w') as log:result=subprocess.run(actual,stdout=log,stderr=subprocess.STDOUT,timeout=4500)
    if action!='capture':assert result.returncode==0,(action,result.returncode)
    else:capture_ok=result.returncode==0
   snapshot(root,dest);parsed,text=parse(dest)
   if not capture_ok and not re.findall(r'RUN mode=',text):
    if state.get('startup_retries',0)>=2:raise RuntimeError('Repeated startup failure')
    state['startup_retries']=state.get('startup_retries',0)+1;continue
   count_before=len(ledger[cohort]);append(ledger,cohort,dest,None if capture_ok else 'Run failed before successful close; '+ (' / '.join(parsed['errors']) or 'capture interrupted'))
   assert len(ledger[cohort])>count_before
  assert len({(x['mode'],x['rotation']) for x in ledger[cohort]})==len(ledger[cohort])
 stage('done',success=True,coverage_complete=True,all_passed=all(x['status']=='PASS' for rows in [ledger['clean'],ledger['diagnostic']] for x in rows),failed=sum(x['status']=='FAIL' for rows in [ledger['clean'],ledger['diagnostic']] for x in rows))
except BaseException as e:
 stage('failed',error=repr(e));(B/'case-collector-failure.txt').write_text(traceback.format_exc());raise
