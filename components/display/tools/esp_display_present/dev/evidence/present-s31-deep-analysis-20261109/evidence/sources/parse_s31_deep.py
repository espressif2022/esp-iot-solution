from pathlib import Path
import re,json
R=Path('/tmp/present-s31-deep-analysis-20261109')
pat=re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')
def kv(s):
 d={k:a if a else b for k,a,b in pat.findall(s)}
 for k,v in list(d.items()):
  if v.isdecimal():d[k]=int(v)
 return d

def parse(path,v,impl):
 cases=[];case=None
 for line in path.read_text(errors='replace').splitlines():
  if 'RUN mode=' in line:
   d=kv(line);case=dict(variant=v,implementation=impl,mode=d['mode'],rotation=d['rotation'],passed=False,scenes={});cases.append(case)
  elif case is not None:
   if 'SCENE mode=' in line:
    d=kv(line);case.setdefault('lvgl_scenes',{})[d['name']]=d
   elif 'DEEP_PROBE:' in line:
    d=kv(line);case['scenes'][d['scene']]=d
   elif 'DEEP_CACHE:' in line:
    d=kv(line);case['scenes'][d['scene']]['cache']=d
   elif 'DEEP_TASK:' in line:
    d=kv(line);case['scenes'][d['scene']].setdefault('tasks',[]).append(d)
   elif 'PASS mode=' in line:case['passed']=True;case['grade']=kv(line)
 for c in cases:
  for s in c['scenes'].values():
   sec=s['us']/1e6
   s['refresh_per_s']=s['refresh']/sec;s['render_per_s']=s['render']/sec;s['panel_per_s']=s['panel']/sec;s['empty_pct']=100*s['empty']/max(1,s['refresh']);s['pixels_per_render']=s['pixels']/max(1,s['render']);s['render_wall_ms']=s['render_us']/max(1,s['render'])/1000;s['wait_ms_per_render']=s['wait_us']/max(1,s['render'])/1000;s['flush_ms_per_render']=s['flush_us']/max(1,s['render'])/1000
   tasks=s['tasks'];s['cpu_busy_pct']=100*(1-sum(t['us'] for t in tasks if t['name'].startswith('IDLE'))/(2*s['us']));s['software_draw_ms_per_render']=sum(t['us'] for t in tasks if t['name']=='swdraw')/max(1,s['render'])/1000;s['present_workers_ms_per_render']=sum(t['us'] for t in tasks if t['name']=='present_service')/max(1,s['render'])/1000
   if s['name'] in c.get('lvgl_scenes',{}):s['lvgl']=c['lvgl_scenes'][s['name']]
 return cases
if __name__=='__main__':
 out=[]
 for v in ['off','on']:
  for impl in ['present','adapter']:
   p=R/v/f's31-rgb/benchmark/{impl}/live.log'
   if p.exists():out+=parse(p,v,impl)
 (R/'parsed.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
 for c in out:
  print(c['variant'],c['implementation'],c['rotation'],'passed',c['passed'])
  for i in [1,7,8,10,15]:
   if i in c['scenes']:
    s=c['scenes'][i];print(' ',s['name'],{k:round(s[k],2) for k in ['render_per_s','refresh_per_s','empty_pct','render_wall_ms','software_draw_ms_per_render','present_workers_ms_per_render','cpu_busy_pct']})
