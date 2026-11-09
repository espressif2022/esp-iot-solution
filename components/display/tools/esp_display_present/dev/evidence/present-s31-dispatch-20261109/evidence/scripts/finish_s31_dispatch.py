from pathlib import Path
import time,subprocess,sys,json,os
R=Path('/tmp/present-s31-dispatch-20261109');O=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-dispatch-20261109')
while not (R/'completion.json').exists():time.sleep(10)
status=json.loads((R/'completion.json').read_text());assert status['success'] and status['restored'],status
with (R/'finalization.log').open('w') as f:
 for cmd in [[sys.executable,'/tmp/parse_s31_dispatch.py','--validate'],[sys.executable,'/tmp/report_s31_dispatch.py'],[sys.executable,'/tmp/validate_s31_dispatch.py',str(O)],['node','/tmp/check_s31_dispatch.mjs']]:
  subprocess.run(cmd,check=True,stdout=f,stderr=subprocess.STDOUT,env={**os.environ,'PRESENT_DISPATCH_REPORT_ROOT':str(O)})
print('Final report generated and validated',O,flush=True)
