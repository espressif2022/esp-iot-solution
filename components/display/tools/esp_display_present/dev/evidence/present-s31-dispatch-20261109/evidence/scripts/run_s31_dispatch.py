from pathlib import Path
import json,subprocess,sys,time,signal,datetime,shutil,hashlib,traceback
from parse_s31_dispatch import parse
R=Path('/tmp/present-s31-dispatch-20261109');P=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-dispatch-20261109')
def stage(action,**kw):
 d=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),action=action,**kw);(R/'stage.json').write_text(json.dumps(d,indent=2)+'\n')
 with (R/'events.jsonl').open('a') as f:f.write(json.dumps(d)+'\n')
 print(json.dumps(d),flush=True)
def run(v,action,impl):
 root=R/v;runner=root/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 cmd=[sys.executable,str(runner),action,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--work-root',str(root),'--port','/dev/ttyUSB0','--profiles','DOUBLE_PARTIAL','--angles','0,90','--lvgl-dir',str(R/'lvgl'),'--timeout','1200']
 stage(action,variant=v,implementation=impl)
 with (root/f'{action}-{impl}-controller.log').open('w') as f:
  if action!='capture':subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=900)
  else:
   proc=subprocess.Popen(cmd,stdout=f,stderr=subprocess.STDOUT);changed=time.monotonic();seen=0;passes=0;path=root/f's31-rgb/benchmark/{impl}/live.log'
   try:
    while proc.poll() is None:
     time.sleep(5)
     if not path.exists():continue
     text=path.read_text(errors='replace');labels=[l for l in text.splitlines() if 'DIAG_CASE ' in l];completed=[l for l in text.splitlines() if 'PASS mode=' in l]
     if len(labels)!=seen:seen=len(labels);changed=time.monotonic();stage('case',variant=v,implementation=impl,current=labels[-1] if labels else '')
     if len(completed)>passes:
      for line in completed[passes:]:stage('case_pass',variant=v,implementation=impl,result=line)
      passes=len(completed)
     if any(s in text for s in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed']):raise RuntimeError('Device runtime error')
     if 'SUITE_PASS ' not in text and time.monotonic()-changed>210:raise RuntimeError('No case progress for210s')
    if proc.returncode:raise RuntimeError('Capture failed')
   finally:
    if proc.poll() is None:proc.send_signal(signal.SIGINT);proc.wait(timeout=15)
changed=False;ok=False;restored=False
try:
 deadline=time.monotonic()+900
 while not (R/'ready-builds.json').exists() or not (R/'backup-proof.json').exists():
  if time.monotonic()>deadline:raise RuntimeError('Build or backup timeout')
  time.sleep(3)
 builds=json.loads((R/'ready-builds.json').read_text());assert len(builds)==4 and all(x[2]==0 for x in builds),builds
 assert json.loads((R/'backup-proof.json').read_text())['current_full_flash_matches_original']
 P.mkdir(exist_ok=True,parents=True)
 for name in ['manifest.json','original.bin']:shutil.copy2(R/'off/s31-rgb/backup'/name,P/name)
 # Freeze sources before flashing and require common public configuration per XIP pair.
 for v in ['off','on']:
  manifests=[json.loads((R/v/f's31-rgb/benchmark/{impl}/manifest.json').read_text()) for impl in ['present','adapter']]
  assert manifests[0]['comparable_sdkconfig_sha256']==manifests[1]['comparable_sdkconfig_sha256']
 for v,impl in [('off','present'),('off','adapter'),('on','present'),('on','adapter')]:
  changed=True;run(v,'flash',impl);run(v,'capture',impl)
  path=R/v/f's31-rgb/benchmark/{impl}';result=parse(path/'live.log',v,impl,True);(path/'diagnostic-results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
  stage('suite_pass',variant=v,implementation=impl,cases=len(result['cases']))
 ok=True
except BaseException as e:
 stage('failed',error=repr(e));(R/'failure.txt').write_text(traceback.format_exc())
finally:
 if changed:
  try:
   run('off','restore','present');src=R/'off/s31-rgb/backup'
   for n in ['restore.log','restore-verify.log','restored.json']:shutil.copy2(src/n,P/n)
   restored=json.loads((src/'restored.json').read_text())['verified_full_flash']
  except BaseException as e:stage('restore_failed',error=repr(e));(R/'restore-failure.txt').write_text(traceback.format_exc())
 stage('done',success=ok,restored=restored,firmware_changed=changed);(R/'completion.json').write_text(json.dumps(dict(success=ok,restored=restored,firmware_changed=changed),indent=2)+'\n')
raise SystemExit(0 if ok and restored else 1)
