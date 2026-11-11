from pathlib import Path
import subprocess,sys,json,traceback,datetime,os
r=Path(__file__).parent;runner=r/'robust_runner.py'
state={'board':'s31-qspi','port':'/dev/ttyACM1','cases':16,'implementation':'present','visual_status':'awaiting user observations; serial cannot establish no dirty pixels','restore_original':False,'success':False}
def save(stage,**kw):
 state.update(stage=stage,updated=datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),**kw);(r/'status.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n');print(stage,flush=True)
try:
 for action in (['capture','report'] if os.environ.get('RESUME_SAME_FIRMWARE')=='1' else ['build','flash','capture','report']):
  save(action)
  with (r/(action+'.log')).open('w') as log:
   subprocess.run([sys.executable,str(runner),action,'--board','s31-qspi','--suite','benchmark','--implementation','present','--work-root',str(r),'--port','/dev/ttyACM1','--timeout','2300'],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=2400)
 data=json.loads((r/'s31-qspi/benchmark/present/results.json').read_text());assert data['complete'] and data['matrix_complete'] and data['buffers_verified'] and not data['errors'],data
 assert len(data['rows'])==16 and all(x['status']=='PASS' and x['scenes']==16 for x in data['rows'])
 save('done',success=True,completed_cases=16,completed_scenes=256)
except BaseException as e:
 save('failed',error=repr(e));(r/'failure.txt').write_text(traceback.format_exc());raise
