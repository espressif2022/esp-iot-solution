from pathlib import Path
import subprocess,sys,json,time,shutil,datetime,concurrent.futures as cf,re,signal,traceback,hashlib
R=Path('/tmp/present-s31-deep-analysis-20261109');P=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-deep-analysis-20261109')
def stage(action,**kw):
 x=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),action=action,**kw);(R/'stage.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x),flush=True)
 with (R/'events.jsonl').open('a') as f:f.write(json.dumps(x)+'\n')
def run(v,a,impl):
 r=R/v;runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py';stage(a,variant=v,implementation=impl)
 cmd=[sys.executable,str(runner),a,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--work-root',str(r),'--port','/dev/ttyUSB0','--profiles','DOUBLE_PARTIAL','--angles','0,90','--lvgl-dir',str(R/'lvgl'),'--timeout','600']
 with (r/f'{a}-{impl}-controller.log').open('w') as out:
  if a!='capture':subprocess.run(cmd,check=True,stdout=out,stderr=subprocess.STDOUT,timeout=1200)
  else:
   proc=subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT);last=None;changed=time.monotonic();passes=0
   try:
    while proc.poll() is None:
     time.sleep(5);p=r/f's31-rgb/benchmark/{impl}/live.log'
     if not p.exists():continue
     s=p.read_text(errors='replace');runs=re.findall(r'RUN mode=.*',s)
     if len(runs)!=last:last=len(runs);changed=time.monotonic()
     done=re.findall(r'PASS mode=.*',s)
     if len(done)>passes:
      for line in done[passes:]:print(v,impl,line,flush=True)
      passes=len(done)
     if 'SUITE_PASS ' not in s and time.monotonic()-changed>210:raise RuntimeError('Stalled case')
    if proc.returncode:raise RuntimeError('capture failed')
   finally:
    if proc.poll() is None:proc.send_signal(signal.SIGINT);proc.wait(timeout=15)
changed=False;ok=False;restored=False
try:
 # Build the adapter in parallel with backup / first present capture.
 with cf.ThreadPoolExecutor(max_workers=1) as pool:
  off_adapter=pool.submit(lambda: None)
  deadline=time.monotonic()+900
  while not (R/'off/s31-rgb/backup/manifest.json').exists():
   if time.monotonic()>deadline:raise RuntimeError('Backup did not finish')
   time.sleep(3)
  b=R/'off/s31-rgb/backup';m=json.loads((b/'manifest.json').read_text());assert m['verified'] and m['mac']=='30:ed:a0:f3:f7:b8';assert hashlib.sha256((b/'original.bin').read_bytes()).hexdigest()==m['sha256']
  P.mkdir(parents=True,exist_ok=True)
  for n in ['original.bin','manifest.json']:shutil.copy2(b/n,P/n)
  dst=R/'on/s31-rgb/backup';dst.mkdir(parents=True,exist_ok=True)
  for n in ['original.bin','manifest.json']:shutil.copy2(b/n,dst/n)
  for v,impl in [('off','present'),('off','adapter'),('on','present'),('on','adapter')]:
   if (v,impl)==('off','adapter'):off_adapter.result();on_adapter=pool.submit(lambda: None)
   if (v,impl)==('on','adapter'):on_adapter.result()
   path=R/v/f's31-rgb/benchmark/{impl}'
   wait=time.monotonic()+900
   while not (path/'manifest.json').exists():
    if time.monotonic()>wait:raise RuntimeError('Build did not finish')
    time.sleep(3)
   changed=True;run(v,'flash',impl);run(v,'capture',impl);run(v,'report',impl)
   x=json.loads((path/'results.json').read_text());assert x['complete'] and x['matrix_complete'] and not x['errors'];assert len(x['rows'])==2 and all(r['scenes']==16 for r in x['rows'])
   s=(path/'live.log').read_text();assert s.count('DEEP_PROBE: scene=')==32;assert s.count('DEEP_CACHE: scene=')==32;assert 'DEEP_TASK:' in s
   stage('variant_done',variant=v,implementation=impl,grades=[r['lvgl_fps'] for r in x['rows']])
 ok=True
except BaseException as e:
 stage('failed',error=repr(e));(R/'failure.txt').write_text(traceback.format_exc())
finally:
 if changed:
  try:
   run('off','restore','present');b=R/'off/s31-rgb/backup'
   for n in ['restore.log','restore-verify.log','restored.json']:shutil.copy2(b/n,P/n)
   restored=json.loads((b/'restored.json').read_text())['verified_full_flash']
  except BaseException as e:stage('restore_failed',error=repr(e));(R/'restore-failure.txt').write_text(traceback.format_exc())
 stage('done',success=ok,restored=restored,firmware_changed=changed)
raise SystemExit(0 if ok and restored else 1)
