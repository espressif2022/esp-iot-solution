from pathlib import Path
import subprocess,sys,json,time,traceback
r=Path('/tmp/present-xip-on-four-angle-20261110');b=sys.argv[1];runner='/tmp/present-xip-on-diag-source-20261110/components/display/tools/esp_display_present/dev/tools/run.py'
try:
 deadline=time.monotonic()+3600
 while not (r/b/'benchmark/adapter-xip-on/manifest.json').exists():
  assert time.monotonic()<deadline,'Clean build timeout';time.sleep(2)
 for impl in ['present','adapter']:
  cmd=[sys.executable,runner,'build','--board',b,'--suite','benchmark','--implementation',impl,'--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0,90,180,270','--work-root',str(r),'--driver-trace',str(r/'driver-overlay')]
  with (r/b/(impl+'-diagnostic-build.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1500)
  print(b,impl,'diagnostic ready',flush=True)
except BaseException as e:(r/b/'diagnostic-build-failed.json').write_text(json.dumps({'error':repr(e),'trace':traceback.format_exc()},indent=2));raise
