from pathlib import Path
import json,subprocess,sys,os,signal,time,shutil,traceback,datetime
R=Path('/tmp/present-s31-align-20261109');P=Path('/tmp/present-s31-stable-20261109');backup=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-dispatch-20261109');pid=None;success=False
try:
 deadline=time.monotonic()+300
 while True:
  d=json.loads((P/'sampling-stage.json').read_text())
  if d['action']=='capture' and d.get('tag')=='sample-pad24':break
  if d['action'] in ['restore','done','failed']:raise RuntimeError('Missed safe sampling capture window')
  if time.monotonic()>deadline:raise RuntimeError('Sampling capture window timeout')
  time.sleep(1)
 for path in Path('/proc').glob('[0-9]*/cmdline'):
  try:argv=path.read_bytes().split(b'\0')
  except (FileNotFoundError,PermissionError):continue
  if b'/tmp/run_s31_sample_v2.py' in argv and b'-u' in argv:pid=int(path.parent.name);os.kill(pid,signal.SIGSTOP);break
 assert pid
 (R/'paused-parent.json').write_text(json.dumps(dict(pid=pid,stage=d)))
 # The existing sampler capture completes and closes its serial descriptor.
 runner=P/'padsample/source/components/display/tools/esp_display_present/dev/tools/run.py'
 while True:
  active=False
  for path in Path('/proc').glob('[0-9]*/cmdline'):
   try:argv=path.read_bytes().split(b'\0')
   except (FileNotFoundError,PermissionError):continue
   if str(runner).encode() in argv and b'capture' in argv:active=True
  if not active:break
  time.sleep(1)
 assert 'PC_DONE' in (P/'padsample/s31-rgb/benchmark/present/live.log').read_text()
 while not (R/'ready-builds.json').exists():time.sleep(3)
 assert json.loads((R/'ready-builds.json').read_text())['ready']
 for name in ['baseline','pad24']:
  dst=R/name/'s31-rgb/backup';dst.mkdir(exist_ok=True,parents=True)
  for n in ['manifest.json','original.bin']:shutil.copy2(backup/n,dst/n)
 for name,impl,tag in [('baseline','present','aligned-before'),('baseline','adapter','aligned-adapter'),('pad24','present','aligned-pad24'),('baseline','present','aligned-after')]:
  for action in ['flash','capture']:
   print(datetime.datetime.now().isoformat(),action,tag,flush=True);(R/'stage.json').write_text(json.dumps(dict(action=action,tag=tag)))
   r=R/name;run=r/'source/components/display/tools/esp_display_present/dev/tools/run.py'
   with (R/f'{tag}-{action}.log').open('w') as f:subprocess.run([sys.executable,str(run),action,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl'),'--port','/dev/ttyUSB0','--timeout','150'],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=240)
  text=(r/f's31-rgb/benchmark/{impl}/live.log').read_text(errors='replace');(R/f'{tag}.log').write_text(text)
  assert 'SUITE_PASS ' in text and text.count('PASS mode=')==2
  assert not any(s in text for s in ['Guru Meditation','assert failed','task_wdt:','ESP_ERROR_CHECK failed'])
 success=True
except BaseException:(R/'failure.txt').write_text(traceback.format_exc());print(traceback.format_exc(),flush=True)
finally:
 if pid:os.kill(pid,signal.SIGCONT);print('Resumed sampler controller for original firmware restoration',flush=True)
 (R/'completion.json').write_text(json.dumps(dict(success=success,restoration='resumed parent controller',parent=str(P))))
assert success
