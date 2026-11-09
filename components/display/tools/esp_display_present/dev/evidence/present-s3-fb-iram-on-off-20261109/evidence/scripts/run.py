from pathlib import Path
import concurrent.futures as cf
import datetime,json,subprocess,sys,shutil,hashlib,signal,time,re,traceback
R=Path('/tmp/present-s3-iram-off-20261109');RUN=R/'source/components/display/tools/esp_display_present/dev/tools/run.py';ON=Path('/tmp/present-five-board-20261109/s3-rgb/benchmark/present');PERSIST=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s3-iram-off-20261109')
def stage(action,**kw):
 x=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),action=action,**kw);(R/'stage.json').write_text(json.dumps(x,indent=2)+'\n')
 with (R/'events.jsonl').open('a') as f:f.write(json.dumps(x)+'\n')
 print(x,flush=True)
def run(action):
 stage(action)
 cmd=[sys.executable,str(RUN),action,'--board','s3-rgb','--suite','benchmark','--implementation','present','--work-root',str(R),'--port','/dev/ttyUSB1','--timeout','3000']
 with (R/(action+'.log')).open('w') as f:
  if action!='capture':subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
  else:
   p=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT);previous=None;changed=time.monotonic()
   while p.poll() is None:
    time.sleep(5);log=R/'s3-rgb/benchmark/present/live.log'
    if not log.exists():continue
    text=log.read_text(errors='replace');runs=re.findall(r'RUN mode=([^ ]+).*?rotation=(\d+)',text);now=(len(runs),runs[-1] if runs else None)
    if now!=previous:previous=now;changed=time.monotonic()
    elif runs and 'SUITE_PASS ' not in text and time.monotonic()-changed>210:
     p.send_signal(signal.SIGINT);p.wait(timeout=15);raise RuntimeError('Case stalled over210s; restore original firmware')
   if p.returncode:raise subprocess.CalledProcessError(p.returncode,cmd)
def cfg(path):
 values={}
 for line in path.read_text().splitlines():
  m=re.fullmatch(r'(CONFIG_\w+)=(.*)',line)
  if m:values[m[1]]=m[2]
  m=re.fullmatch(r'# (CONFIG_\w+) is not set',line)
  if m:values[m[1]]='n'
 return values
changed=False;success=False;restored=False
try:
 with cf.ThreadPoolExecutor(max_workers=2) as pool:
  build=pool.submit(run,'build');run('backup');b=R/'s3-rgb/backup';info=json.loads((b/'manifest.json').read_text());assert info['verified'] and info['mac']=='90:e5:b1:d0:a9:d8';assert hashlib.sha256((b/'original.bin').read_bytes()).hexdigest()==info['sha256'];PERSIST.mkdir(parents=True,exist_ok=True)
  for name in ['original.bin','manifest.json']:shutil.copy2(b/name,PERSIST/name)
  build.result()
 off=R/'s3-rgb/benchmark/present';a=json.loads((ON/'manifest.json').read_text());b=json.loads((off/'manifest.json').read_text())
 for key in ['cases','board','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','test_sources_sha256','component_sources_sha256']:assert a[key]==b[key],key
 a_cfg,b_cfg=cfg(ON/'sdkconfig'),cfg(off/'sdkconfig');difference={k:[a_cfg.get(k),b_cfg.get(k)] for k in a_cfg.keys()|b_cfg.keys() if a_cfg.get(k)!=b_cfg.get(k)}
 assert difference=={'CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM':['y','n']},difference
 (R/'configuration-proof.json').write_text(json.dumps(dict(passed=True,differences=difference,baseline=str(ON),cases=24,original_verified=info),indent=2)+'\n')
 changed=True;run('flash');run('capture');run('report')
 result=json.loads((off/'results.json').read_text());assert result['complete'] and result['matrix_complete'] and result['memory_complete'] and not result['errors'];assert len(result['rows'])==24 and all(r['scenes']==16 for r in result['rows']);success=True
except BaseException as e:
 stage('failed',error=repr(e));(R/'failure.txt').write_text(traceback.format_exc())
finally:
 if changed:
  try:
   run('restore');b=R/'s3-rgb/backup'
   for name in ['restore.log','restore-verify.log','restored.json']:shutil.copy2(b/name,PERSIST/name)
   assert json.loads((b/'restored.json').read_text())['verified_full_flash'];restored=True
  except BaseException as e:stage('restore_failed',error=repr(e));(R/'restore-failure.txt').write_text(traceback.format_exc())
 stage('done',tests_succeeded=success,firmware_changed=changed,restored=restored)
raise SystemExit(0 if success and restored else 1)
