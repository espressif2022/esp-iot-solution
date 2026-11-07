import sys,subprocess,json,time,traceback
from pathlib import Path
board,port=sys.argv[1:];base=Path('/tmp/present-all-modes-20261107');runner=base/'source/components/display/tools/esp_display_present/dev/tools/run.py'
def event(stage,**data):
 record=dict(time=time.time(),board=board,stage=stage,**data)
 with (base/(board+'-events.jsonl')).open('a') as f:f.write(json.dumps(record)+'\n')
 print(json.dumps(record),flush=True)
def ready(path):
 start=time.monotonic()
 while not path.is_file():
  if time.monotonic()-start>1800:raise TimeoutError(str(path))
  time.sleep(1)
def command(action,impl):
 cmd=[sys.executable,str(runner),action,'--board',board,'--suite','benchmark','--implementation',impl,'--port',port,'--work-root',str(base)]
 if action=='capture':cmd+=['--timeout','4000']
 with (base/f'{board}-{impl}-{action}.log').open('w') as log:
  r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 if r.returncode:
  event('COMMAND_FAILED',action=action,implementation=impl)
  print((base/f'{board}-{impl}-{action}.log').read_text(errors='replace')[-3500:],flush=True)
  raise subprocess.CalledProcessError(r.returncode,cmd)
ready(base/board/'backup/manifest.json');started=False
try:
 for impl in ['present','adapter'] if board!='s31-qspi' else ['present']:
  ready(base/board/'benchmark'/impl/'manifest.json')
  event('FLASH_START',implementation=impl);started=True;command('flash',impl)
  event('CAPTURE_START',implementation=impl);command('capture',impl);command('report',impl)
  result=json.loads((base/board/'benchmark'/impl/'results.json').read_text())
  assert result['memory_complete'] and result['matrix_complete'] and result['complete'] and not result['errors']
  event('SUITE_PASS',implementation=impl,cases=len(result['rows']))
 if board!='s31-qspi':command('compare','present')
 event('ALL_TESTS_PASS')
except BaseException as e:
 event('FAILED',error=repr(e));traceback.print_exc();raise
finally:
 if started:
  event('RESTORE_START');command('restore','present');event('RESTORE_VERIFIED')
