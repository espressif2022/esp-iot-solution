from pathlib import Path
import subprocess,sys,json
r=Path('/tmp/present-s31-rgb-analysis-20261109/xip');runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py'
for impl in ['present','adapter']:
 with (r/('prebuild-'+impl+'.log')).open('w') as f:
  x=subprocess.run([sys.executable,str(runner),'build','--board','s31-rgb','--suite','benchmark','--implementation',impl,'--work-root',str(r),'--profiles','DOUBLE_PARTIAL,TRIPLE_PARTIAL','--angles','0,90'],stdout=f,stderr=subprocess.STDOUT)
 (r/('prebuild-'+impl+'-result.json')).write_text(json.dumps({'exit_code':x.returncode})+'\n');print(impl,x.returncode,flush=True)
 if x.returncode:raise SystemExit(x.returncode)
