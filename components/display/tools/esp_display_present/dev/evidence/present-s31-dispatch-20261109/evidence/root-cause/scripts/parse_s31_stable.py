from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,re,bisect,collections
R=Path('/tmp/present-s31-stable-20261109');out=[]
for tag,name,impl in [('sample-present','baseline','present'),('sample-adapter','baseline','adapter'),('sample-early','early','present'),('sample-pad24','padsample','present')]:
 p=R/f'{tag}.log'
 if not p.exists():continue
 elf=R/name/f's31-rgb/benchmark/{impl}/build/present_benchmark.elf'
 with elf.open('rb') as f:
  e=ELFFile(f);syms=e.get_section_by_name('.symtab');functions=[]
  for s in syms.iter_symbols():
   if s['st_info']['type']=='STT_FUNC' and s['st_size'] and isinstance(s['st_shndx'],int):
    if e.get_section(s['st_shndx']).name=='.flash.text':functions.append((s['st_value'],s['st_value']+s['st_size'],s.name))
 functions.sort();starts=[x[0] for x in functions]
 def symbolize(pc):
  first=max(0,bisect.bisect_right(starts,pc)-1);end=bisect.bisect_left(starts,pc+64);c=[]
  for a,b,n in functions[first:end]:
   overlap=max(0,min(pc+64,b)-max(pc,a))
   if overlap:c.append((overlap,n,a,b))
  if not c:return '<padding-or-unknown>',False
  best=max(c);return best[1],len(c)>1 or best[0]!=64
 cases=[];current=None
 for line in p.read_text().splitlines():
  if 'DIAG_CASE ' in line:
   if current:cases.append(current)
   current=dict(bins=[],cores=[],probe={})
  if current is None:continue
  if 'PC_BIN:' in line:
   m=re.search(r'core=(\d+) pc=([0-9a-f]+) count=(\d+)',line);c,pc,n=int(m[1]),int(m[2],16),int(m[3]);fn,mixed=symbolize(pc);current['bins'].append(dict(core=c,pc=hex(pc),count=n,function=fn,mixed_boundary=mixed,set_index=(pc>>6)&63))
  if 'PC_SAMPLE: core=' in line:current['cores'].append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
  if 'DEEP_PROBE: scene=0 ' in line:current['probe']={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
 if current:cases.append(current)
 if len(cases)==2 and not cases[0]['bins'] and cases[1]['bins']:
  cases[0]['bins'],cases[1]['bins']=cases[1]['bins'],[]
  cases[0]['cores'],cases[1]['cores']=cases[1]['cores'],[]
 byfunc=collections.Counter();bycore=[collections.Counter(),collections.Counter()];mixed=0
 for c in cases:
  for b in c['bins']:byfunc[b['function']]+=b['count'];bycore[b['core']][b['function']]+=b['count'];mixed+=b['count'] if b['mixed_boundary'] else 0
 samples=sum(s['samples'] for c in cases for s in c['cores']);outside=sum(s['outside'] for c in cases for s in c['cores'])
 for c in cases:
  for s in c['cores']:assert sum(b['count'] for b in c['bins'] if b['core']==s['core'])+s['outside']+s['overflow']==s['samples']
 totalus=cases[0]['probe'].get('us',0);renders=cases[0]['probe'].get('render',0)
 out.append(dict(tag=tag,cases=cases,samples=samples,outside=outside,mixed_boundary_samples=mixed,render_per_s=renders*1e6/totalus if totalus else 0,top_functions=[dict(function=k,samples=v,percent_all=v*100/samples,ms_per_render=v/renders if renders else 0,core0=bycore[0][k],core1=bycore[1][k]) for k,v in byfunc.most_common(40)]))
(R/'pc-results.json').write_text(json.dumps(out,indent=2))
for x in out:
 print(x['tag'],'cases',len(x['cases']),'samples',x['samples'],'outside',x['outside'],'render/s',round(x['render_per_s'],3))
 for f in x['top_functions'][:12]:print(' ',f['function'],f['samples'],round(f['percent_all'],2),'%',round(f['ms_per_render'],2),'ms/frame')
