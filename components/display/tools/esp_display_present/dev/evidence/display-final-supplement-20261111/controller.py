from pathlib import Path
import subprocess,sys,json,time,traceback
r=Path(__file__).parent;b,port=sys.argv[1:];state={'board':b,'port':port,'restore_original':False,'success':False,'runs':[]}
def save(stage,**kw):
 state.update(stage=stage,updated=time.time(),**kw);(r/b/'status.json').write_text(json.dumps(state,indent=2)+'\n');print(b,stage,kw,flush=True)
try:
 for diag in ([False,True] if b=='p4-dsi' else [False]):
  impl='adapter' if b=='s31-qspi' else 'present';variant=impl+('-xip-on' if b=='p4-dsi' else '')+('-driver-trace' if diag else '');root=r/b/'benchmark'/variant
  args=['--board',b,'--suite','benchmark','--implementation',impl,'--work-root',str(r),'--port',port,'--timeout','3300','--angles','0,90,180,270']
  if b=='p4-dsi':args+=['--xip','on','--profiles','DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO']
  if b=='s31-qspi':args+=['--profiles','ADAPTER_TE_SYNC_FULL,ADAPTER_TE_ASYNC_FULL,ADAPTER_TE_ASYNC_BOUNCE']
  if diag:args+=['--driver-trace',str(r/'driver-overlay')]
  for action in (['capture','report'] if __import__('os').environ.get('RESUME_CAPTURE')=='1' else ['build','flash','capture','report']):
   save(action,variant=variant)
   with (r/b/(variant+'-'+action+'.log')).open('w') as log:subprocess.run([sys.executable,str(r/'runner.py'),action,*args],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3500)
  d=json.loads((root/'results.json').read_text());n=24 if diag else {'p4-dsi':19,'s31-qspi':9,'box3-spi':20}[b]
  assert d['complete'] and d['matrix_complete'] and d['buffers_verified'] and not d['errors'],d
  assert len(d['rows'])==n and all(x['status']=='PASS' and x['scenes']==16 and 'after_close' in x.get('memory',{}) for x in d['rows']),d
  state['runs'].append({'variant':variant,'passed':n,'scenes':n*16})
 save('done',success=True)
except BaseException as e:
 save('failed',error=repr(e));(r/b/'failure.txt').write_text(traceback.format_exc());raise
