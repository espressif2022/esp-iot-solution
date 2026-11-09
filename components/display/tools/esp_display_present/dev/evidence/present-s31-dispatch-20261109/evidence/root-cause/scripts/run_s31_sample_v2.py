from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,subprocess,sys,shutil,hashlib,time,traceback,datetime,struct
R=Path('/tmp/present-s31-stable-20261109');backup=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-s31-dispatch-20261109')
def stage(action,**kw):
 d=dict(time=datetime.datetime.now().isoformat(),action=action,**kw);(R/'sampling-stage.json').write_text(json.dumps(d));print(json.dumps(d),flush=True)
def command(cmd,path,timeout=300):
 with path.open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=timeout)
def invoke(name,impl,action,tag):
 r=R/name;runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py';stage(action,tag=tag)
 command([sys.executable,str(runner),action,'--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl'),'--port','/dev/ttyUSB0','--timeout','150'],R/f'v2-{tag}-{action}.log',240)
changed=False;success=False;restored=False
try:
 while not (R/'ready-builds.json').exists() or not (R/'padsample-ready.json').exists():time.sleep(3)
 while not (R/'completion.json').exists():time.sleep(3)
 assert json.loads((R/'completion.json').read_text())['restored']
 assert json.loads((R/'pad-normalization.json').read_text())['success']
 proofs=[]
 for name,impl in [('baseline','present'),('baseline','adapter'),('early','present'),('padsample','present')]:
  root=R/name/f's31-rgb/benchmark/{impl}';ep=root/'build/present_benchmark.elf'
  with ep.open('rb') as f:
   e=ELFFile(f);sy=e.get_section_by_name('.symtab');install=sy.get_symbol_by_name('pc_sampler_install')[0];assert e.get_section(install['st_shndx']).name=='.iram0.text'
   start=sy.get_symbol_by_name('__init_array_start')[0]['st_value'];end=sy.get_symbol_by_name('__init_array_end')[0]['st_value'];sec=next(s for s in e.iter_sections() if s['sh_addr']<=start and end<=s['sh_addr']+s['sh_size']);data=sec.data()[start-sec['sh_addr']:end-sec['sh_addr']];assert install['st_value'] in struct.unpack('<'+'I'*(len(data)//4),data)
   watch=sy.get_symbol_by_name('measuring')[0]['st_value'];assert f'0x{watch:08x}u' in (root/'pc_address.h').read_text()
   proofs.append(dict(variant=name,implementation=impl,constructor=hex(install['st_value']),watch=hex(watch),constructor_registered=True))
 (R/'constructor-proof.json').write_text(json.dumps(proofs,indent=2))
 base=[sys.executable,'-m','esptool','--chip','esp32s31','-p','/dev/ttyUSB0','-b','115200','--before','default-reset','--after','hard-reset'];stage('identify_and_verify_original');command(base+['read-mac'],R/'v2-identify.log',60);assert '30:ed:a0:f3:f7:b8' in (R/'v2-identify.log').read_text().lower();command(base+['verify-flash','0',str(backup/'original.bin')],R/'v2-verify-original.log',180)
 for name,impl,tag in [('baseline','present','sample-present'),('baseline','adapter','sample-adapter'),('early','present','sample-early'),('padsample','present','sample-pad24')]:
  dst=R/name/'s31-rgb/backup';dst.mkdir(exist_ok=True,parents=True)
  for n in ['manifest.json','original.bin']:shutil.copy2(backup/n,dst/n)
  changed=True;invoke(name,impl,'flash',tag);invoke(name,impl,'capture',tag)
  text=(R/name/f's31-rgb/benchmark/{impl}/live.log').read_text(errors='replace');(R/f'{tag}.log').write_text(text)
  assert 'SUITE_PASS ' in text and text.count('PASS mode=')==2 and 'PC_DONE' in text and text.count('PC_SAMPLE: core=')==2
  assert not any(x in text for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed','task_wdt:'])
  assert text.count('overflow=0 state=2')==2
 success=True
except BaseException:(R/'sampling-failure.txt').write_text(traceback.format_exc());stage('failed',trace=traceback.format_exc())
finally:
 if changed:
  try:invoke('baseline','present','restore','original');restored=json.loads((R/'baseline/s31-rgb/backup/restored.json').read_text())['verified_full_flash']
  except BaseException:(R/'sampling-restore-failure.txt').write_text(traceback.format_exc())
 (R/'sampling-completion.json').write_text(json.dumps(dict(success=success,restored=restored,firmware_changed=changed)));stage('done',success=success,restored=restored)
assert success and restored
