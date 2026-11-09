from pathlib import Path
import json,hashlib,subprocess,shutil,gzip,sys
R=Path('/tmp/present-aligned-fill-20261109');C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');O=C/'deliveries/present-aligned-fill-20261109';repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR');S=R/'source/components/display/tools';D=S/'esp_display_present/dev';O=Path(sys.argv[1]) if len(sys.argv)>1 else O;dst=O/'evidence/test-inputs'
man=json.loads((O/'aligned-manifests.json').read_text());reference=man['s3-rgb/present'];files={**reference['test_sources_sha256']}
for tag,item in man.items():
 assert item['component_sources_sha256']==reference['component_sources_sha256'],tag
 assert item['test_sources_sha256']==reference['test_sources_sha256'],tag
for name,sha in files.items():
 p=D/name;assert hashlib.sha256(p.read_bytes()).hexdigest()==sha,name
 q=dst/name;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
for part in ['boards/configs','test_apps/benchmark/CMakeLists.txt','test_apps/benchmark/sdkconfig.defaults']:
 p=D/part
 if not p.exists():continue
 if p.is_dir():shutil.copytree(p,dst/part,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__'))
 else:q=dst/part;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
# Record source identity against the clean committed runtime. Only one intentional adapter allocation change.
checks=[]
for path,sha in reference['component_sources_sha256'].items():
 current=S/path;assert hashlib.sha256(current.read_bytes()).hexdigest()==sha,path
 committed=subprocess.check_output(['git','-C',str(repo),'show','d84edb1d0:components/display/tools/'+path]);same=hashlib.sha256(committed).hexdigest()==sha
 assert same or path=='esp_lvgl_adapter/src/display/display_manager.c',path
 checks.append(dict(path=path,sha256=sha,identical_to_production_revision=same))
assert sum(not x['identical_to_production_revision'] for x in checks)==1
(O/'runtime-source-validation.json').write_text(json.dumps(dict(revision='d84edb1d0e41ecea8a7ed6be543ceca950041db1',runtime_files=checks,only_change='esp_lvgl_adapter/src/display/display_manager.c PARTIAL input allocation; see adapter-allocation-only.patch'),indent=2)+'\n')
idf=Path('/home/xuxin/esp_work/esp-idf-qspi')
revision=subprocess.check_output(['git','-C',str(idf),'rev-parse','HEAD'],text=True).strip()
tracked=subprocess.check_output(['git','-C',str(idf),'diff','HEAD','--name-only'],text=True).splitlines()
assert not tracked and revision=='7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe'
(O/'idf-validation.json').write_text(json.dumps(dict(revision=revision,tracked_changes=tracked,driver_patches=False),indent=2)+'\n')
# Exact config difference between the two S3 present variants.
def config(p):return {l.split('=',1)[0]:l for l in p.read_text().splitlines() if l.startswith('CONFIG_') or l.startswith('# CONFIG_')}
a=config(R/'s3-rgb/benchmark/present/sdkconfig');b=config(R/'iram-off/s3-rgb/benchmark/present/sdkconfig');diff={k:[a.get(k),b.get(k)] for k in a.keys()|b.keys() if a.get(k)!=b.get(k)}
# Enabled and disabled lines use different keys. They must refer to just this symbol.
assert all('CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM' in k for k in diff),diff
(O/'iram-config-validation.json').write_text(json.dumps(diff,indent=2)+'\n')
(O/'evidence/scripts').mkdir(parents=True,exist_ok=True)
for name in ['validate_present_aligned.py','archive_present_aligned.py','analyze_present_aligned.py','check_present_aligned.mjs','publish_present_aligned.py','finalize_present_aligned.sh','check_published_present_aligned.py']:shutil.copy2('/tmp/'+name,O/'evidence/scripts'/name)
for name in ['controller.log','resume-controller.log']:(O/'evidence'/(name+'.gz')).write_bytes(gzip.compress((R/name).read_bytes(),mtime=0))
for board in ['s31-rgb','s3-rgb']:
 for path in (R/board/'backup').glob('*.log'):
  if path.name.startswith('restore'):continue
  (O/'evidence'/board/'restore'/(path.name+'.gz')).write_bytes(gzip.compress(path.read_bytes(),mtime=0))
 for suffix in ['reuse-identify.log','reuse-verify.log']:
  p=R/f'{board}-{suffix}'
  if p.exists():(O/'evidence'/board/'restore'/(suffix+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
for p in (O/'evidence/scripts').iterdir():
 if p.is_file():p.chmod(0o644)
# Preserve old data independently so older results remain traceable.
history=O/'history-before-alignment';history.mkdir(exist_ok=True)
for name in ['comparison-current.json','comparison-current.csv','validation.json']:
 shutil.copy2(R/'before'/name,history/name)
print('Archived source proof, test harness, configurations, and restoration logs',len(checks))
