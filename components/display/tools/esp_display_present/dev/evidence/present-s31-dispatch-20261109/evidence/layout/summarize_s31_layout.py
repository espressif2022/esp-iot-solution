from pathlib import Path
import json,re
R=Path('/tmp/present-s31-layout-20261109');out=[]
for tag in ['base-before','adapter','early','base-after']:
 p=R/f'{tag}.log'
 if not p.exists():continue
 text=p.read_text();rows=[];cur=None
 for line in text.splitlines():
  if 'DIAG_CASE ' in line:
   if cur: rows.append(cur)
   cur={}
  if cur is None:continue
  if any(key in line for key in ['DEEP_PROBE: scene=0 ','DEEP_CACHE: scene=0 ','DEEP_EXEC: scene=0 ']):
   prefix='exec_' if 'DEEP_EXEC:' in line else ''
   cur.update({prefix+k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
 if cur:rows.append(cur)
 rows=[r for r in rows if r.get('render',0)>0];n=sum(r['render'] for r in rows)
 if n:
  sums={k:sum(r.get(k,0) for r in rows) for k in ['render','pixels','flush','us','ia0','ia1','is0','is1','if0','if1','ic0','ic1']}
  fills=sums['if0']+sums['if1'];stalls=sums['is0']+sums['is1'];access=sums['ia0']+sums['ia1']
  out.append(dict(tag=tag,cases=len(rows),raw=sums,render_per_s=n*1e6/sums['us'],pixels_per_render=sums['pixels']/n,flushes_per_render=sums['flush']/n,instruction_fills_per_render=fills/n,instruction_stalls_per_render=stalls/n,instruction_accesses_per_render=access/n,stall_per_fill=stalls/fills,instruction_conflicts=sums['ic0']+sums['ic1'],core0_fills_per_render=sums['if0']/n,core1_fills_per_render=sums['if1']/n))
(R/'summary.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
