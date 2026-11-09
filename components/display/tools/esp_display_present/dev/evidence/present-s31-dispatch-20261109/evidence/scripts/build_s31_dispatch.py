from pathlib import Path
import subprocess,sys,concurrent.futures as cf,json,datetime
R=Path('/tmp/present-s31-dispatch-20261109')
def build(v,impl):
 root=R/v;runner=root/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 print(datetime.datetime.now().isoformat(),v,impl,'build',flush=True)
 with (root/f'build-{impl}-controller.log').open('w') as f:
  p=subprocess.run([sys.executable,str(runner),'build','--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0,90','--work-root',str(root),'--lvgl-dir',str(R/'lvgl')],stdout=f,stderr=subprocess.STDOUT)
 print(datetime.datetime.now().isoformat(),v,impl,'finished',p.returncode,flush=True)
 return [v,impl,p.returncode]
with cf.ThreadPoolExecutor(max_workers=2) as pool:out=list(pool.map(lambda x:build(*x),[('off','present'),('off','adapter'),('on','present'),('on','adapter')]))
(R/'build-completion.json').write_text(json.dumps(out,indent=2)+'\n')
raise SystemExit(int(any(r[2] for r in out)))
