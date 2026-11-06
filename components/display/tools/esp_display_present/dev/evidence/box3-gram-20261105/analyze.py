from pathlib import Path
import json,re,csv,hashlib,tarfile,io
r=Path(__file__).parent
scene_re=re.compile(r'SCENE mode=(\w+) te_buffers=(\d+) rotation=(\d+) name="(.*?)" fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms')
valid={}
for impl,expected,skips in [('adapter',4,40),('present',20,24)]:
    d=r/impl
    if not (d/'summary.json').exists():continue
    s=json.loads((d/'summary.json').read_text());scenes=[]
    for line in (d/'live.log').read_text(errors='replace').splitlines():
        m=scene_re.search(line)
        if m: scenes.append([m[1],int(m[2]),int(m[3]),m[4],*map(int,m.group(5,6,7,8))])
    with (d/'scenes.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(['mode','te_buffers','rotation','scene','fps','cpu_percent','render_ms','flush_ms']);w.writerows(scenes)
    validation={'complete':s['complete'],'passes':len(s['pass']),'expected_passes':expected,'skips':len(s['skip']),'expected_skips':skips,'scenes':len(scenes),'expected_scenes':expected*16,'all_passes_16_scenes':all(c['scenes']==16 for c in s['pass']),'errors':s['errors'],'error_lines':s['error_lines'],'boot_count':s['boot_count']}
    validation['passed']=all([validation['complete'],validation['passes']==expected,validation['skips']==skips,validation['scenes']==expected*16,validation['all_passes_16_scenes'],not validation['errors'],not validation['error_lines'],validation['boot_count']==1])
    valid[impl]=validation
(r/'validation.json').write_text(json.dumps(valid,ensure_ascii=False,indent=2)+'\n')
if len(valid)==2:
    p=json.loads((r/'present/summary.json').read_text());a=json.loads((r/'adapter/summary.json').read_text())
    lookup={(c['mode'],c['rotation']):c for c in a['pass']}
    fields=['mode','rotation','present_lvgl_fps','adapter_lvgl_fps','present_transport_fps','present_render_ms','present_flush_ms','adapter_render_ms','adapter_flush_ms']
    with (r/'comparison.csv').open('w',newline='') as f:
        w=csv.writer(f,lineterminator='\n');w.writerow(fields)
        for c in p['pass']:
            ac=lookup.get((c['mode'],c['rotation']),{})
            w.writerow([c['mode'],c['rotation'],c['lvgl_fps'],ac.get('lvgl_fps','-'),c['transport_fps'],c['render_ms'],c['flush_ms'],ac.get('render_ms','-'),ac.get('flush_ms','-')])
print(json.dumps(valid,ensure_ascii=False,indent=2))
