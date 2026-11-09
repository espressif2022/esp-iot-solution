from pathlib import Path
import hashlib,json,re,sys
roots=[Path(p) for p in sys.argv[1:]]
for root in roots:
 index={}
 for line in (root/'SHA256SUMS').read_text().splitlines():
  sha,name=line.split('  ',1);p=root/name;assert p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest()==sha,(root,name);index[name]=sha
 files={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p!=root/'SHA256SUMS'};assert files==set(index),(files-set(index),set(index)-files)
 for name in ['comparison-current.html']:
  for href in re.findall(r'href="([^"]+)"',(root/name).read_text()):
   if href.startswith(('#','https:','http:')):continue
   assert (root/href).exists(),(root,href)
 print('Published hashes and links verified',root,len(index))
