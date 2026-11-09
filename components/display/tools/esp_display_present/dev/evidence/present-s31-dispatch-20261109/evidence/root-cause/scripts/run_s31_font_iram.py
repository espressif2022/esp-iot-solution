from pathlib import Path
import json,subprocess,sys,shutil,hashlib,time,traceback,datetime
R=Path('/tmp/present-s31-font-iram-20261109');backup=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-dispatch-20261109')
def stage(action,**kw):
 d=dict(time=datetime.datetime.now().isoformat(),action=action,**kw);(R/'stage.json').write_text(json.dumps(d));print(json.dumps(d),flush=True)
def command(cmd,path,timeout=300):
 with path.open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=timeout)
def invoke(name,impl,action,tag):
 r=R/name;runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 stage(action,tag=tag)
 command([sys.executable,str(runner),action,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl'),'--port','/dev/ttyUSB0','--timeout','150'],R/f'{tag}-{action}.log',240)
changed=False;success=False;restored=False
try:
 while not (R/'ready-builds.json').exists():time.sleep(3)
 previous=Path('/tmp/present-s31-stable-20261109/sampling-completion.json')
 while not previous.exists():time.sleep(3)
 assert json.loads(previous.read_text())['restored']
 assert all(c[2]==0 for c in json.loads((R/'build-completion.json').read_text()))
 info=json.loads((backup/'manifest.json').read_text());assert info['mac']=='30:ed:a0:f3:f7:b8'
 assert hashlib.sha256((backup/'original.bin').read_bytes()).hexdigest()==info['sha256']
 assert json.loads((backup/'restored.json').read_text())['verified_full_flash']
 base=[sys.executable,'-m','esptool','--chip','esp32s31','-p','/dev/ttyUSB0','-b','115200','--before','default-reset','--after','hard-reset']
 stage('identify_and_verify_original');command(base+['read-mac'],R/'identify.log',60);assert info['mac'] in (R/'identify.log').read_text().lower()
 command(base+['verify-flash','0',str(backup/'original.bin')],R/'verify-original.log',180)
 for variant in ['baseline','pad24']:
  dst=R/variant/'s31-rgb/backup';dst.mkdir(exist_ok=True,parents=True)
  for n in ['manifest.json','original.bin']:shutil.copy2(backup/n,dst/n)
 for name,impl,tag in [('baseline','present','font-before'),('baseline','adapter','font-adapter'),('pad24','present','font-pad24'),('baseline','present','font-after')]:
  changed=True;invoke(name,impl,'flash',tag);invoke(name,impl,'capture',tag)
  text=(R/name/f's31-rgb/benchmark/{impl}/live.log').read_text(errors='replace');(R/f'{tag}.log').write_text(text)
  assert 'SUITE_PASS ' in text and text.count('PASS mode=')==2
  assert not any(x in text for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed'])
 success=True
except BaseException:
 (R/'failure.txt').write_text(traceback.format_exc());stage('failed',trace=traceback.format_exc())
finally:
 if changed:
  try:
   invoke('baseline','present','restore','original');restored=json.loads((R/'baseline/s31-rgb/backup/restored.json').read_text())['verified_full_flash']
  except BaseException:(R/'restore-failure.txt').write_text(traceback.format_exc())
 (R/'completion.json').write_text(json.dumps(dict(success=success,restored=restored,firmware_changed=changed)));stage('done',success=success,restored=restored)
assert success and restored
