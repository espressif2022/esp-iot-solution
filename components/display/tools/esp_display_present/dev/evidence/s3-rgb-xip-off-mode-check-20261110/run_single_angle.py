from pathlib import Path
import json,subprocess,time,sys,traceback
r=Path('/tmp/present-s3-xip-off-all-modes-20261110');runner=Path('/tmp/present-s3-off-matrix-source-20261110/components/display/tools/esp_display_present/dev/tools/run.py');state={'stage':'waiting_single_build','success':False,'requested_angles':[0],'new_cases':4,'reused_present_cases':6,'reused_adapter_cases':1,'restore_original':False,'archive_complete':False}
def save(stage):state.update(stage=stage,updated=time.time());(r/'single-angle-controller.json').write_text(json.dumps(state,indent=2)+'\n');print(stage,flush=True)
save('waiting_single_build')
try:
 deadline=time.monotonic()+600
 while True:
  m=json.loads((r/'s3-rgb/benchmark/adapter-xip-off-driver-trace/manifest.json').read_text())
  if len(m['cases'])==5 and all(x['rotation']==0 for x in m['cases']):break
  assert time.monotonic()<deadline,'Single-angle build timed out'
  time.sleep(2)
 for action in ['flash','capture','report']:
  save(action);cmd=[sys.executable,str(runner),action,'--board','s3-rgb','--suite','benchmark','--implementation','adapter','--xip','off','--profiles','TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0','--work-root',str(r),'--driver-trace',str(r/'driver-overlay'),'--port','/dev/ttyUSB1','--timeout','500']
  with (r/('single-'+action+'.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=550)
 d=json.loads((r/'s3-rgb/benchmark/adapter-xip-off-driver-trace/results.json').read_text());assert d['complete'] and d['matrix_complete'] and d['buffers_verified'] and not d['errors'];assert len(d['rows'])==5 and sum(x['status']=='PASS' for x in d['rows'])==4 and all(x['scenes']==16 for x in d['rows'] if x['status']=='PASS');state['success']=True
except BaseException as e:state['error']=repr(e);(r/'single-failure.txt').write_text(traceback.format_exc())
finally:
 try:
  save('leave_xip_on_reference');ref=Path('/tmp/present-s3-jitter-work-20261110');ref_runner=Path('/tmp/present-s3-jitter-source-20261110/components/display/tools/esp_display_present/dev/tools/run.py');cmd=[sys.executable,str(ref_runner),'flash','--board','s3-rgb','--suite','benchmark','--implementation','present','--xip','on','--profiles','DOUBLE_FULL','--angles','0','--work-root',str(ref),'--driver-trace',str(ref/'driver-overlay'),'--port','/dev/ttyUSB1']
  with (r/'single-reference-flash.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
  state['retained_firmware']='Previously measured present XIP ON / 16MHz / ten-line bounce diagnostic reference'
 except BaseException as e:state['reference_flash_error']=repr(e)
 save('done')
 try:
  subprocess.run([sys.executable,str(r/'report_single_angle.py'),'--archive'],check=True,timeout=120);state['archive_complete']=True
 except BaseException as e:state['archive_error']=repr(e)
 save('done')
assert state['success'] and state['archive_complete'],state
