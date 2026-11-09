from pathlib import Path
import re,json,sys
pattern=re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')
def kv(s):
 d={k:a if a else b for k,a,b in pattern.findall(s)}
 for k,v in d.items():
  if v.isdecimal():d[k]=int(v)
  elif re.fullmatch(r'\d+\.\d+',v):d[k]=float(v)
 return d

def parse(path,variant,implementation,validate=False):
 cases=[];current=None;errors=[];text=Path(path).read_text(errors='replace')
 for line in text.splitlines():
  if any(x in line for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed','Stack canary']) or (re.search(r'\bE \(',line) and 'GPIO isr service already installed' not in line):errors.append(line.strip())
  if 'DIAG_CASE ' in line:
   current=dict(variant=variant,implementation=implementation,**kv(line),memory={},scenes={},lvgl_scenes={},passed=False);cases.append(current)
  elif current is not None:
   if 'MEMORY mode=' in line:
    d=kv(line);current['memory'][d['phase']]=d
   elif 'SCENE mode=' in line:
    d=kv(line);current['lvgl_scenes'][d['name']]=d
   elif 'DEEP_PROBE:' in line:
    d=kv(line);current['scenes'][d['scene']]=d
   elif 'DEEP_CACHE:' in line:
    d=kv(line);current['scenes'][d['scene']]['cache']=d
   elif 'DEEP_EXEC:' in line:
    d=kv(line);current['scenes'][d['scene']]['execute']=d
   elif 'DEEP_TASK:' in line:
    d=kv(line);current['scenes'][d['scene']].setdefault('tasks',[]).append(d)
   elif 'BUFFER_AUDIT:' in line:current['buffer_audit']=kv(line)
   elif 'PASS mode=' in line:current['passed']=True;current['grade']=kv(line)
 for c in cases:
  for s in c['scenes'].values():
   if not all(k in s for k in ['cache','execute','tasks']):continue
   den=max(1,s['render']);counters=s['cache'];sec=s['us']/1e6
   s['render_per_s']=s['render']/sec;s['panel_per_s']=s['panel']/sec;s['pixels_per_render']=s['pixels']/den
   s['flushes_per_render']=s['flush']/den;s['software_cpu_ms_per_render']=sum(t['us'] for t in s['tasks'] if t['name']=='swdraw')/den/1000
   s['workers_cpu_ms_per_render']=sum(t['us'] for t in s['tasks'] if t['name']=='present_service')/den/1000
   # adapter UI is lvgl; present UI is benchmark. Keep exact task list as evidence.
   s['ui_cpu_ms_per_render']=sum(t['us'] for t in s['tasks'] if t['name'] in ['benchmark','lvgl'])/den/1000
   s['ui_workers_cpu_ms_per_render']=s['ui_cpu_ms_per_render']+s['workers_cpu_ms_per_render']
   s['execute_wall_ms_per_render']=s['execute']['us']/den/1000
   for traffic,prefix in [('instruction','i'),('data','d')]:
    access=sum(counters[f'{prefix}a{i}'] for i in [0,1]);stall=sum(counters[f'{prefix}s{i}'] for i in [0,1]);fills=sum(counters[f'{prefix}f{i}'] for i in [0,1]);conflict=sum(counters[f'{prefix}c{i}'] for i in [0,1])
    s[traffic+'_accesses_per_render']=access/den;s[traffic+'_stalls_per_render']=stall/den;s[traffic+'_fills_per_render']=fills/den;s[traffic+'_conflicts_per_render']=conflict/den
    s[traffic+'_fill_access_ratio']=fills/access if access else None;s[traffic+'_stall_fill_ratio']=stall/fills if fills else None
   if s['name'] in c['lvgl_scenes']:s['lvgl']=c['lvgl_scenes'][s['name']]
 if validate:
  expected=8 if implementation=='present' else 4
  assert 'SUITE_PASS ' in text and not errors,(path,errors)
  assert len(cases)==expected,(path,len(cases))
  for n,c in enumerate(cases):
   reps=4 if implementation=='present' else 2;angle=[0,90][n//reps];execution=('inline' if n%reps in [1,2] else 'worker') if implementation=='present' else 'adapter'
   assert c['pass']==n and c['rotation']==angle and c['execution']==execution,c
   assert c['passed'] and c['grade']['scenes']==16 and len(c['scenes'])==len(c['lvgl_scenes'])==16,(path,n)
   audit=c['buffer_audit'];size=80000 if angle==0 else 48000;assert audit['min_bytes']==audit['max_bytes']==audit['active_bytes']==size and audit['input_tiles']==1
   assert all(k in c['memory'] for k in ['before_open','after_open','after_run','after_close'])
   for s in c['scenes'].values():
    assert s['us']>0 and s['render']>0 and s['cache']['saturated']==s['errors']==s['execute']['errors']==0,(path,n,s['name'])
    assert s['capacity']==size
    assert s['instruction_accesses_per_render']>0 and s['instruction_fills_per_render']>0
    if implementation=='present':assert s['execute']['calls']>0 and s['execute']['hw']+s['execute']['cpu']==s['execute']['calls'],s
 return dict(complete='SUITE_PASS ' in text,errors=errors,cases=cases)
if __name__=='__main__':
 root=Path('/tmp/present-s31-dispatch-20261109');result=[]
 for v in ['off','on']:
  for impl in ['present','adapter']:
   p=root/v/f's31-rgb/benchmark/{impl}/live.log'
   if p.exists():result.append(parse(p,v,impl,'--validate' in sys.argv))
 (root/'parsed.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print('Parsed',sum(len(x['cases']) for x in result),'cases')
