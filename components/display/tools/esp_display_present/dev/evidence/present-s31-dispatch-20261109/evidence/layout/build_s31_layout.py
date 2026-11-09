from pathlib import Path
import subprocess,sys,concurrent.futures,json
R=Path('/tmp/present-s31-layout-20261109')
def build(case):
 name,impl=case;r=R/name;runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 with (r/f'build-{impl}.log').open('w') as f:
  x=subprocess.run([sys.executable,str(runner),'build','--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl')],stdout=f,stderr=subprocess.STDOUT)
 print(case,x.returncode,flush=True);return [name,impl,x.returncode]
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as e:out=list(e.map(build,[('baseline','present'),('baseline','adapter'),('early','present')]))
(R/'build-completion.json').write_text(json.dumps(out));assert all(x[2]==0 for x in out)
