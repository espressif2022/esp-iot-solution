from pathlib import Path
import json,subprocess,time,sys,traceback
r=Path('/tmp/present-s3-xip-on-address-20261110');c=Path('/tmp/present-s3-on-address-source-20261110/components/display/tools/esp_display_present');runner=c/'dev/tools/run.py'
state={'stage':'wait_builds','success':False,'xip':'on','angles':[0],'restore_original':False}
def save(stage):state.update(stage=stage,updated=time.time());(r/'controller.json').write_text(json.dumps(state,indent=2)+'\n');print(stage,flush=True)
save('wait_builds')
try:
 while not json.loads(Path('/tmp/present-s3-xip-on-light-20261110/controller.json').read_text()).get('success'):
  time.sleep(2)
 deadline=time.monotonic()+1200
 for impl in ['present','adapter']:
  v=r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace')
  while not (v/'manifest.json').exists():
   assert time.monotonic()<deadline,'Build did not finish';time.sleep(2)
  manifest=json.loads((v/'manifest.json').read_text());assert len(manifest['cases'])==6 and all(x['rotation']==0 for x in manifest['cases'])
  for action in ['flash','capture','report']:
   save(impl+'_'+action)
   cmd=[sys.executable,str(runner),action,'--board','s3-rgb','--suite','benchmark','--implementation',impl,'--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0','--work-root',str(r),'--driver-trace',str(r/'driver-overlay'),'--port','/dev/ttyUSB1','--timeout','1000']
   with (r/(impl+'-'+action+'.log')).open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1100)
  result=json.loads((v/'results.json').read_text());assert result['complete'] and result['matrix_complete'] and result['buffers_verified'] and not result['errors'],result.get('errors');assert sum(x['status']=='PASS' for x in result['rows'])==(6 if impl=='present' else 5)
 state['success']=True;state['retained_firmware']='adapter XIP ON address diagnostic firmware; no original restore';save('done')
except BaseException as e:state['error']=repr(e);(r/'failure.txt').write_text(traceback.format_exc());save('failed');raise
