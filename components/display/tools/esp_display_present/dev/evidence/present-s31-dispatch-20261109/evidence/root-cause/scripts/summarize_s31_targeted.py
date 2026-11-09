from pathlib import Path
import re,json
roots=[('/tmp/present-s31-stable-20261109',['original-before','pad24','original-after','sample-present','sample-adapter','sample-early','sample-pad24']),('/tmp/present-s31-align-20261109',['aligned-before','aligned-adapter','aligned-pad24','aligned-after']),('/tmp/present-s31-font-iram-20261109',['font-before','font-adapter','font-pad24','font-after'])]
out=[]
for root,tags in roots:
 R=Path(root)
 for tag in tags:
  p=R/f'{tag}.log'
  if not p.exists():continue
  t=p.read_text();assert t.count('PASS mode=')==2 and 'SUITE_PASS ' in t
  assert not any(s in t for s in ['assert failed','Guru Meditation','task_wdt:','ESP_ERROR_CHECK failed'])
  rows=[];current=None
  for l in t.splitlines():
   if 'DIAG_CASE ' in l:
    if current:rows.append(current)
    current=dict(probe={},cache={},memory={},tasks=[])
   if current is None:continue
   values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',l)}
   if 'DEEP_PROBE: scene=0 ' in l:current['probe']=values
   if 'DEEP_CACHE: scene=0 ' in l:current['cache']=values
   if 'DEEP_TASK: scene=0 ' in l:
    m=re.search(r'name="([^"]+)"',l)
    if m:current['tasks'].append(dict(name=m[1],**values))
   if 'present_bench: MEMORY mode=DOUBLE_PARTIAL' in l:
    m=re.search(r'phase=(\S+)',l);current['memory'][m[1]]=values
  if current:rows.append(current)
  assert len(rows)==2 and all(r['probe']['render']>20 and r['probe']['pixels']==r['probe']['render']*384000 and r['probe']['flush']==r['probe']['render']*10 and r['probe']['capacity']==80000 and r['probe']['errors']==r['cache']['saturated']==0 for r in rows)
  n=sum(r['probe']['render'] for r in rows);duration=sum(r['probe']['us'] for r in rows)
  def cache(k):return sum(r['cache'][k] for r in rows)
  out.append(dict(root=str(R),tag=tag,cases=rows,render_per_s=n*1e6/duration,instruction_accesses_per_render=(cache('ia0')+cache('ia1'))/n,instruction_fills_per_render=(cache('if0')+cache('if1'))/n,instruction_stalls_per_render=(cache('is0')+cache('is1'))/n,instruction_conflicts=cache('ic0')+cache('ic1'),software_cpu_ms_per_render=sum(s['us'] for r in rows for s in r['tasks'] if s['name']=='swdraw')/n/1000))
Path('/tmp/s31-targeted-results.json').write_text(json.dumps(out,indent=2))
for x in out:print(x['tag'],round(x['render_per_s'],3),round(x['instruction_fills_per_render']),round(x['instruction_stalls_per_render']/1e6,3),round(x['software_cpu_ms_per_render'],2))
