from pathlib import Path
import subprocess,shutil,json,time
b=Path('/tmp/present-all-modes-20261107');w=b/'p4-followup';saved=b/'p4-followup-attempt';src=w/'p4-dsi/benchmark/present';saved.mkdir()
# Preserve failed multi-case run before reusing its build cache.
for name in ['manifest.json','build.log','live.log','bench_cases.h','sdkconfig']:
 if (src/name).exists():shutil.copyfile(src/name,saved/name)
runner=b/'source/components/display/tools/esp_display_present/dev/tools/run.py'
cases=[('DOUBLE_PARTIAL',a) for a in [90,180,270]]+[(mode,a) for mode in ['TRIPLE_PARTIAL','AUTO'] for a in [0,90,180,270]]
def event(stage,**data):
 rec=dict(time=time.time(),board='p4-dsi',stage=stage,isolated=True,**data)
 with (b/'p4-dsi-events.jsonl').open('a') as f:f.write(json.dumps(rec)+'\n')
 print(json.dumps(rec),flush=True)
def run(action,work,impl,extra=(),log=None):
 cmd=['python3',str(runner),action,'--board','p4-dsi','--implementation',impl,'--work-root',str(work),'--port','/dev/ttyACM0',*extra]
 with log.open('w') as output:r=subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(errors='replace')[-1500:],flush=True)
 return r.returncode
started=False;results=[]
try:
 for mode,angle in cases:
  dest=b/'p4-isolated-cases'/f'{mode}-{angle}';dest.mkdir(parents=True)
  assert run('build',w,'present',['--profiles',mode,'--angles',str(angle)],dest/'build-command.log')==0
  while not (b/'p4-followup.done').exists():time.sleep(1)
  event('FLASH_START',implementation='present',mode=mode,rotation=angle);started=True
  assert run('flash',w,'present',log=dest/'flash.log')==0
  event('CAPTURE_START',implementation='present',mode=mode,rotation=angle)
  cap=run('capture',w,'present',['--timeout','240'],dest/'capture.log')
  report=run('report',w,'present',log=dest/'report.log')
  for name in ['manifest.json','build.log','live.log','results.json','bench_cases.h']:
   if (src/name).exists():shutil.copyfile(src/name,dest/name)
  results.append(dict(mode=mode,rotation=angle,passed=cap==0 and report==0))
  event('CASE_PASS' if results[-1]['passed'] else 'CASE_FAIL',mode=mode,rotation=angle)
 (b/'p4-isolated-summary.json').write_text(json.dumps(results,indent=2)+'\n')
 event('FLASH_START',implementation='adapter');started=True
 assert run('flash',b,'adapter',log=b/'p4-dsi-adapter-flash.log')==0
 event('CAPTURE_START',implementation='adapter')
 assert run('capture',b,'adapter',['--timeout','4000'],b/'p4-dsi-adapter-capture.log')==0
 assert run('report',b,'adapter',log=b/'p4-dsi-adapter-report.log')==0
 event('ADAPTER_SUITE_PASS')
finally:
 if started:
  event('RESTORE_START')
  assert run('restore',w,'present',log=b/'p4-dsi-final-restore.log')==0
  event('RESTORE_VERIFIED')
