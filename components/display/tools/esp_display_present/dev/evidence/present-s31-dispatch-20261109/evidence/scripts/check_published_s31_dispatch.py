from pathlib import Path
import json,hashlib,re,sys
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');R=Path('/tmp/present-s31-dispatch-20261109')
if '--guard' in sys.argv:
 d={}
 for root in [C/'deliveries/present-adapter-five-board-20261109',Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/evidence/five-board-regression-20261109')]:
  for n in ['comparison-current.json','comparison-current.csv','comparison-current.xlsx']:d[str(root/n)]=hashlib.sha256((root/n).read_bytes()).hexdigest()
 (R/'formal-data-guard.json').write_text(json.dumps(d,indent=2)+'\n');print('Formal score and memory data frozen');raise SystemExit(0)
for name,sha in json.loads((R/'formal-data-guard.json').read_text()).items():assert hashlib.sha256(Path(name).read_bytes()).hexdigest()==sha,name
for arg in sys.argv[1:]:
 root=Path(arg);index={}
 for line in (root/'SHA256SUMS').read_text().splitlines():
  sha,name=line.split('  ',1);assert hashlib.sha256((root/name).read_bytes()).hexdigest()==sha,(root,name);index[name]=sha
 files={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p!=root/'SHA256SUMS'};assert files==set(index)
 for n in ['comparison.html','comparison-current.html']:
  if not (root/n).exists():continue
  for href in re.findall(r'href="([^"]+)"',(root/n).read_text()):
   if href.startswith(('#','http:','https:')):continue
   assert (root/href).exists(),(root,href)
 print('Verified hashes and links',root,len(index))
print('Original formal scores and memory files unchanged')
