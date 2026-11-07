from pathlib import Path
import subprocess,shutil,json,time
b=Path('/tmp/present-all-modes-20261107');saved=b/'p4-adapter-continuous-attempt';src=b/'p4-dsi/benchmark/adapter';saved.mkdir()
for name in ['manifest.json','build.log','live.log','bench_cases.h','sdkconfig']:
 if (src/name).exists():shutil.copyfile(src/name,saved/name)
runner=b/'source/components/display/tools/esp_display_present/dev/tools/run.py'
cases=[('DOUBLE_DIRECT',270)]+[(mode,a) for mode in ['DOUBLE_PARTIAL','TRIPLE_PARTIAL'] for a in [0,90,180,270]]
def event(stage,**data):
 rec=dict(time=time.time(),board='p4-dsi',stage=stage,adapter_isolated=True,**data)
 with (b/'p4-dsi-events.jsonl').open('a') as f:f.write(json.dumps(rec)+'\n')
 print(json.dumps(rec),flush=True)
def run(action,extra,log):
 cmd=['python3',str(runner),action,'--board','p4-dsi','--implementation','adapter','--work-root',str(b),'--port','/dev/ttyACM0',*extra]
 with log.open('w') as output:r=subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(errors='replace')[-1200:],flush=True)
 return r.returncode
started=False;results=[]
try:
 for mode,angle in cases:
  dest=b/'p4-adapter-isolated-cases'/f'{mode}-{angle}';dest.mkdir(parents=True)
  assert run('build',['--profiles',mode,'--angles',str(angle)],dest/'build-command.log')==0
  while not (b/'p4-adapter-continuous.done').exists():time.sleep(1)
  event('FLASH_START',implementation='adapter',mode=mode,rotation=angle);started=True
  assert run('flash',[],dest/'flash.log')==0
  event('CAPTURE_START',implementation='adapter',mode=mode,rotation=angle)
  cap=run('capture',['--timeout','240'],dest/'capture.log')
  report=run('report',[],dest/'report.log')
  for name in ['manifest.json','build.log','live.log','results.json','bench_cases.h']:
   if (src/name).exists():shutil.copyfile(src/name,dest/name)
  results.append(dict(mode=mode,rotation=angle,passed=cap==0 and report==0))
  event('CASE_PASS' if results[-1]['passed'] else 'CASE_FAIL',mode=mode,rotation=angle)
 (b/'p4-adapter-isolated-summary.json').write_text(json.dumps(results,indent=2)+'\n')
finally:
 if started:
  event('RESTORE_START')
  assert run('restore',[],b/'p4-adapter-final-restore.log')==0
  event('RESTORE_VERIFIED')
