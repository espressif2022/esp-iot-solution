from pathlib import Path
import json,csv,statistics,sys
from parse_s31_dispatch import parse
R=Path('/tmp/present-s31-dispatch-20261109');O=Path(sys.argv[1]) if len(sys.argv)>1 else R/'preview';O.mkdir(exist_ok=True,parents=True)
raw=[];groups={}
for variant in ['off','on']:
 for impl in ['present','adapter']:
  p=R/variant/f's31-rgb/benchmark/{impl}/live.log'
  if not p.exists():continue
  result=parse(p,variant,impl);raw.append(result)
  for c in result['cases']:
   if not c['passed']:continue
   for s in c['scenes'].values():
    key=(variant,c['execution'],c['rotation'],s['name']);groups.setdefault(key,[]).append((c,s))
summary=[];flat=[]
for (variant,execution,angle,name),records in groups.items():
 ss=[s for c,s in records];n=sum(s['render'] for s in ss);us=sum(s['us'] for s in ss)
 d=dict(executor_instrumented=execution!='adapter',xip=variant,execution=execution,rotation=angle,scene=name,repeats=len(records),renders=n,window_us=us,render_per_s=n*1e6/us,render_per_s_min=min(s['render_per_s'] for s in ss),render_per_s_max=max(s['render_per_s'] for s in ss),pixels_per_render=sum(s['pixels'] for s in ss)/n,flushes_per_render=sum(s['flush'] for s in ss)/n,software_cpu_ms_per_render=sum(sum(t['us'] for t in s['tasks'] if t['name']=='swdraw') for s in ss)/n/1000,ui_workers_cpu_ms_per_render=sum(sum(t['us'] for t in s['tasks'] if t['name'] in ['benchmark','lvgl','present_service']) for s in ss)/n/1000,execute_wall_ms_per_render=sum(s['execute']['us'] for s in ss)/n/1000)
 for traffic,prefix in [('instruction','i'),('data','d')]:
  access=sum(s['cache'][prefix+'a'+str(i)] for s in ss for i in [0,1]);stall=sum(s['cache'][prefix+'s'+str(i)] for s in ss for i in [0,1]);fills=sum(s['cache'][prefix+'f'+str(i)] for s in ss for i in [0,1]);conflict=sum(s['cache'][prefix+'c'+str(i)] for s in ss for i in [0,1])
  d.update({traffic+'_accesses_per_render':access/n,traffic+'_stalls_per_render':stall/n,traffic+'_fills_per_render':fills/n,traffic+'_conflicts_per_render':conflict/n,traffic+'_fill_access_ratio':fills/access if access else None,traffic+'_stall_fill_ratio':stall/fills if fills else None,traffic+'_stall_access_ratio':stall/access if access else None})
 summary.append(d)
 for c,s in records:
  rec=dict(xip=variant,execution=execution,rotation=angle,repeat=c['repeat'],pass_id=c['pass'],scene=name)
  for k,v in s.items():
   if isinstance(v,(int,float,str)):rec[k]=v
  rec.update({'cache_'+k:v for k,v in s['cache'].items() if k!='scene'});rec.update({'execute_'+k:v for k,v in s['execute'].items() if k!='scene'});flat.append(rec)
(O/'raw-results.json').write_text(json.dumps(raw,ensure_ascii=False,indent=2)+'\n');(O/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n')
fields=list(dict.fromkeys(k for r in flat for k in r))
with (O/'scenes.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');w.writeheader();w.writerows(flat)
focus=['Screen sized text','Multiple labels','Multiple arcs','Moving wallpaper']
for name in focus:
 print('\n',name)
 for d in summary:
  if d['scene']!=name:continue
  print(d['xip'],d['execution'],d['rotation'],'n',d['repeats'],'render/s',round(d['render_per_s'],2),'accessM',round(d['instruction_accesses_per_render']/1e6,2),'stallM',round(d['instruction_stalls_per_render']/1e6,2),'fills',round(d['instruction_fills_per_render']),'miss%',round(d['instruction_fill_access_ratio']*100,4),'stall/fill',round(d['instruction_stall_fill_ratio'],2),'conflicts',round(d['instruction_conflicts_per_render']),'swms',round(d['software_cpu_ms_per_render'],2))
print('Completed cases',sum(c['passed'] for result in raw for c in result['cases']))
