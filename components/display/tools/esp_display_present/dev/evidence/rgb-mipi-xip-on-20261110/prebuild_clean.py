from pathlib import Path
import subprocess,sys,json
r=Path('/tmp/present-xip-on-comparison-20261110');b=sys.argv[1];runner='/tmp/present-xip-on-clean-source-20261110/components/display/tools/esp_display_present/dev/tools/run.py'
for impl in ['present','adapter']:
 cmd=[sys.executable,runner,'build','--board',b,'--suite','benchmark','--implementation',impl,'--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0','--work-root',str(r)]
 with (r/b/(impl+'-clean-build.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1500)
 print(b,impl,'clean ready',flush=True)
