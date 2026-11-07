from pathlib import Path
import subprocess,time,json
b=Path('/tmp/present-all-modes-20261107');w=b/'p4-followup';w.mkdir();(w/'deps').symlink_to(b/'deps',target_is_directory=True)
runner=b/'source/components/display/tools/esp_display_present/dev/tools/run.py'
def event(stage,**data):
 record=dict(time=time.time(),board='p4-dsi',stage=stage,followup=True,**data)
 with (b/'p4-dsi-events.jsonl').open('a') as f:f.write(json.dumps(record)+'\n')
 print(json.dumps(record),flush=True)
def command(action,impl,work):
 cmd=['python3',str(runner),action,'--board','p4-dsi','--implementation',impl,'--work-root',str(work),'--port','/dev/ttyACM0']
 if action=='build':cmd+=['--profiles','DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO']
 if action=='capture':cmd+=['--timeout','4000']
 logpath=b/f'p4-dsi-{impl}-followup-{action}.log'
 with logpath.open('w') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 if r.returncode:print(logpath.read_text(errors='replace')[-2000:],flush=True)
 assert r.returncode==0,(action,impl)
command('build','present',w)
(w/'p4-dsi/backup').symlink_to(b/'p4-dsi/backup',target_is_directory=True)
while not (b/'p4-diagnostic.done').exists():time.sleep(1)
started=False
try:
 for impl,work in [('present',w),('adapter',b)]:
  event('FLASH_START',implementation=impl);started=True;command('flash',impl,work)
  event('CAPTURE_START',implementation=impl);command('capture',impl,work);command('report',impl,work)
  event('SUITE_PASS',implementation=impl)
 event('FOLLOWUP_TESTS_PASS')
finally:
 if started:event('RESTORE_START');command('restore','present',w);event('RESTORE_VERIFIED')
