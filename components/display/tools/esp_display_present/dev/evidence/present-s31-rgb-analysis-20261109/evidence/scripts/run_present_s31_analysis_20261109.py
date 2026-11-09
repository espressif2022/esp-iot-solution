from pathlib import Path
import concurrent.futures as cf
import datetime,json,subprocess,sys,shutil,hashlib,signal,time,re,traceback
R=Path('/tmp/present-s31-rgb-analysis-20261109');PERSIST=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-rgb-analysis-20261109');PORT='/dev/ttyUSB0'
def stage(action,**kw):
 x=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),action=action,**kw);(R/'stage.json').write_text(json.dumps(x,indent=2)+'\n')
 with (R/'events.jsonl').open('a') as f:f.write(json.dumps(x)+'\n')
 print(json.dumps(x),flush=True)
def root(v):return R/v

def run(v,action,impl='present',profile=True):
 r=root(v);runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py';stage(action,variant=v,implementation=impl,profile=profile)
 cmd=[sys.executable,str(runner),action,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--work-root',str(r),'--port',PORT,'--profiles','DOUBLE_PARTIAL,TRIPLE_PARTIAL','--angles','0,90','--timeout','1000']
 if profile:cmd+=['--profile']
 log=r/f'{impl}-'+Path('') if False else r/f'{action}-{impl}{"-profile" if profile else ""}.log'
 with log.open('w') as f:
  if action!='capture':subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=1200)
  else:
   p=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT);previous=None;changed=time.monotonic();lastpasses=0
   try:
    while p.poll() is None:
     time.sleep(5);live=r/'s31-rgb/benchmark'/(impl+('-profile' if profile else ''))/'live.log'
     if not live.exists():continue
     text=live.read_text(errors='replace');runs=re.findall(r'RUN mode=([^ ]+).*?rotation=(\d+)',text);now=(len(runs),runs[-1] if runs else None)
     if now!=previous:previous=now;changed=time.monotonic()
     passes=re.findall(r'PASS mode=.*',text)
     if len(passes)>lastpasses:
      for line in passes[lastpasses:]:print(v,impl,line.strip(),flush=True)
      lastpasses=len(passes)
     if time.monotonic()-changed>210 and 'SUITE_PASS ' not in text:raise RuntimeError('Capture stalled over210s')
    if p.returncode:raise subprocess.CalledProcessError(p.returncode,cmd)
   finally:
    if p.poll() is None:p.send_signal(signal.SIGINT);p.wait(timeout=15)
 return r/'s31-rgb/benchmark'/(impl+('-profile' if profile else ''))

def backup_to(v):
 src=R/'baseline/s31-rgb/backup';dst=R/v/'s31-rgb/backup';dst.mkdir(parents=True,exist_ok=True)
 for name in ['original.bin','manifest.json']:shutil.copy2(src/name,dst/name)

def test(v,impl='present',profile=True,build=True):
 if build:run(v,'build',impl,profile)
 backup_to(v) if v!='baseline' else None
 global changed
 changed=True
 run(v,'flash',impl,profile);run(v,'capture',impl,profile);out=run(v,'report',impl,profile)
 x=json.loads((out/'results.json').read_text());assert x['complete'] and x['matrix_complete'] and x['memory_complete'] and not x['errors'];assert len(x['rows'])==4 and all(t['scenes']==16 for t in x['rows'])
 stage('variant_complete',variant=v,implementation=impl,profile=profile,grades=[{k:t[k] for k in ['mode','rotation','lvgl_fps','render','flush']} for t in x['rows']])
changed=False;success=False;restored=False
try:
 with cf.ThreadPoolExecutor(max_workers=2) as pool:
  build=pool.submit(run,'baseline','build');run('baseline','backup');b=R/'baseline/s31-rgb/backup';info=json.loads((b/'manifest.json').read_text());assert info['verified'] and info['mac']=='30:ed:a0:f3:f7:b8';assert hashlib.sha256((b/'original.bin').read_bytes()).hexdigest()==info['sha256'];PERSIST.mkdir(parents=True,exist_ok=True)
  for name in ['original.bin','manifest.json']:shutil.copy2(b/name,PERSIST/name)
  build.result()
 test('baseline',build=False)
 test('baseline',impl='adapter',profile=False)
 for v in ['burst128','cache_contract','burst_cache','tile2']:test(v)
 # Uninstrumented control and combined candidate confirm instrumentation did not create the apparent gain.
 test('baseline',profile=False)
 test('burst_cache',profile=False)
 success=True
except BaseException as e:
 stage('failed',error=repr(e));(R/'failure.txt').write_text(traceback.format_exc())
finally:
 if changed:
  try:
   run('baseline','restore');b=R/'baseline/s31-rgb/backup'
   for name in ['restore.log','restore-verify.log','restored.json']:shutil.copy2(b/name,PERSIST/name)
   assert json.loads((b/'restored.json').read_text())['verified_full_flash'];restored=True
  except BaseException as e:stage('restore_failed',error=repr(e));(R/'restore-failure.txt').write_text(traceback.format_exc())
 stage('done',tests_succeeded=success,firmware_changed=changed,restored=restored)
raise SystemExit(0 if success and restored else 1)
