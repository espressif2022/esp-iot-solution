import sys,subprocess,json,time,traceback
from pathlib import Path
board,port=sys.argv[1:];base=Path('/tmp/present-refactor-hardware-20261106');events=base/(board+'-events.jsonl')
def event(stage,**detail):
 record=dict(time=time.time(),board=board,stage=stage,**detail)
 with events.open('a') as log:log.write(json.dumps(record)+'\n')
 print(json.dumps(record),flush=True)
def ready(path,seconds=1200):
 start=time.monotonic()
 while not path.is_file():
  if time.monotonic()-start>seconds:raise TimeoutError(str(path))
  time.sleep(1)
def command(variant,suite,action,timeout=None):
 root=base/variant/board/suite/'present';component=base/(variant+'-tree')/'components/display/tools/esp_display_present'
 cmd=[sys.executable,str(component/'dev/tools/run.py'),action,'--board',board,'--suite',suite,'--port',port,'--work-root',str(base/variant)]
 if timeout:cmd+=['--timeout',str(timeout)]
 with (base/f'{board}-{variant}-{suite}-{action}.log').open('w') as output:subprocess.run(cmd,cwd=component,stdout=output,stderr=subprocess.STDOUT,check=True)
 return root
ready(base/'current'/board/'backup/manifest.json',900)
started=False
try:
 for suite in ['pixels','benchmark']:
  for variant in ['baseline','current']:
   manifest=base/variant/board/suite/'present/manifest.json';ready(manifest)
   event('FLASH_START',variant=variant,suite=suite);started=True
   command(variant,suite,'flash');event('CAPTURE_START',variant=variant,suite=suite)
   root=command(variant,suite,'capture',180 if suite=='pixels' else 900)
   command(variant,suite,'report')
   if suite=='pixels':
    text=(root/'live.log').read_text(errors='replace')
    assert text.count('CASE_PASS ')==28,(variant,board,text.count('CASE_PASS '))
    assert 'cases=28 rotations=4 frames_per_case=4' in text
   event('SUITE_PASS',variant=variant,suite=suite)
 event('ALL_TESTS_PASS')
except BaseException as error:
 event('FAILED',error=repr(error));traceback.print_exc();raise
finally:
 if started:
  event('RESTORE_START')
  command('current','benchmark','restore')
  event('RESTORE_VERIFIED')
