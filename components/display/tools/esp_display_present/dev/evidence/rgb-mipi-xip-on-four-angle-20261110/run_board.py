from pathlib import Path
import subprocess,sys,json,time,traceback
r=Path('/tmp/present-xip-on-four-angle-20261110');board,port=sys.argv[1:];b=r/board
state={'board':board,'port':port,'xip':'on','angles':[0,90,180,270],'runs':[],'restore_original':False,'success':False}
def save(stage,**extra):
 state.update(stage=stage,updated=time.time(),**extra);(b/'controller.json').write_text(json.dumps(state,indent=2)+'\n');print(board,stage,flush=True)
for trace in [False,True]:
 for impl in ['present','adapter']:
  name=impl+'-xip-on'+('-driver-trace' if trace else '');root=b/'benchmark'/name;item={'implementation':impl,'diagnostic':trace,'variant':name,'success':False};state['runs'].append(item)
  try:
   save('wait_build',variant=name);deadline=time.monotonic()+3600
   while not (root/'manifest.json').exists():
    assert time.monotonic()<deadline,'Build wait timeout';assert not (b/('diagnostic-build-failed.json' if trace else 'clean-build-failed.json')).exists(),'Build failed';time.sleep(2)
   runner=Path('/tmp/present-xip-on-'+('diag' if trace else 'clean')+'-source-20261110/components/display/tools/esp_display_present/dev/tools/run.py')
   for action in ['flash','capture','report']:
    save(action,variant=name)
    cmd=[sys.executable,str(runner),action,'--board',board,'--suite','benchmark','--implementation',impl,'--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0,90,180,270','--work-root',str(r),'--port',port,'--timeout','4400']
    if trace:cmd+=['--driver-trace',str(r/'driver-overlay')]
    with (b/(name+'-'+action+'.log')).open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=4500)
   data=json.loads((root/'results.json').read_text());assert data['complete'] and data['matrix_complete'] and data['buffers_verified'] and not data['errors'],data.get('errors');assert all(x['scenes']==16 for x in data['rows'] if x['status']=='PASS');assert sum(x['status']=='PASS' for x in data['rows'])==(24 if impl=='present' else 20)
   item.update(success=True,passed=sum(x['status']=='PASS' for x in data['rows']),skipped=sum(x['status']=='SKIP' for x in data['rows']))
  except BaseException as e:item.update(error=repr(e));(b/(name+'-failure.txt')).write_text(traceback.format_exc());save('variant_failed')
save('done',success=len(state['runs'])==4 and all(x['success'] for x in state['runs']),retained_firmware='last flashed XIP ON test firmware; original firmware not restored')
assert state['success'],state
