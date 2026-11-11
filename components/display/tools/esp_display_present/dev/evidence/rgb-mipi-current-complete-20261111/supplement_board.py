from pathlib import Path
import subprocess,sys,json,time,traceback,gzip,os
r=Path(__file__).parent;board,port=sys.argv[1:];b=r/board;runner=r/'missing_runner.py';state={'board':board,'port':port,'runs':[],'restore_original':False,'success':False,'reuse_latest_clean_cases':8,'new_clean_cases':(4 if board=='s31-rgb' and (b/'resume-clean.json').exists() else (10 if board=='p4-dsi' and (r/'diagnostics/p4-direct270-close-timeout/failure.json').exists() else 16)),'new_diagnostic_cases':24}
def save(stage,**extra):
 state.update(stage=stage,updated=time.time(),**extra);(b/'supplement-controller.json').write_text(json.dumps(state,indent=2)+'\n');print(board,stage,flush=True)
try:
 prior=b/'benchmark/present-xip-on/live.log'
 if prior.exists() and not (b/'interrupted-redundant-live.log.gz').exists():(b/'interrupted-redundant-live.log.gz').write_bytes(gzip.compress(prior.read_bytes(),mtime=0))
 for trace in [False,True]:
  name='present-xip-on'+('-driver-trace' if trace else '');root=b/'benchmark'/name;item={'variant':name,'success':False};state['runs'].append(item)
  for action in (['capture','report'] if os.environ.get('PRESENT_RESUME_CAPTURE')=='1' and not trace else ['build','flash','capture','report']):
   save(action,variant=name)
   cmd=[sys.executable,str(runner),action,'--board',board,'--suite','benchmark','--implementation','present','--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO','--angles','0,90,180,270','--work-root',str(r),'--port',port,'--timeout','4400']
   if trace:cmd+=['--driver-trace',str(r/'driver-overlay')]
   with (b/(name+'-supplement-'+action+'.log')).open('w') as log:subprocess.run(cmd,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=4500)
  data=json.loads((root/'results.json').read_text());assert data['complete'] and data['matrix_complete'] and data['buffers_verified'] and not data['errors'],data.get('errors');assert all(x['scenes']==16 for x in data['rows']);assert len(data['rows'])==(24 if trace else (4 if board=='s31-rgb' and (b/'resume-clean.json').exists() else (10 if board=='p4-dsi' and (r/'diagnostics/p4-direct270-close-timeout/failure.json').exists() else 16)))
  item['success']=True
 save('done',success=True)
except BaseException as e:
 save('failed',error=repr(e));(b/'supplement-failure.txt').write_text(traceback.format_exc());raise
