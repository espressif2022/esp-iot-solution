from pathlib import Path
import subprocess,sys,json
r=Path('/tmp/present-s31-rgb-analysis-20261109')
for v in ['baseline','burst_cache']:
 p=r/v;runner=p/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 with (p/'prebuild-control.log').open('w') as f:
  x=subprocess.run([sys.executable,str(runner),'build','--board','s31-rgb','--suite','benchmark','--implementation','present','--work-root',str(p),'--profiles','DOUBLE_PARTIAL,TRIPLE_PARTIAL','--angles','0,90'],stdout=f,stderr=subprocess.STDOUT)
 (p/'prebuild-control-result.json').write_text(json.dumps({'exit_code':x.returncode})+'\n');print(v,x.returncode,flush=True)
 if x.returncode:raise SystemExit(x.returncode)
