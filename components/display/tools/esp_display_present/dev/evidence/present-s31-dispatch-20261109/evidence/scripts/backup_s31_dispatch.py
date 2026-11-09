from pathlib import Path
import subprocess,sys,json,hashlib,shutil
R=Path('/tmp/present-s31-dispatch-20261109');src=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-aligned-fill-20261109/s31-rgb')
info=json.loads((src/'manifest.json').read_text());assert info['verified'] and info['mac']=='30:ed:a0:f3:f7:b8';assert hashlib.sha256((src/'original.bin').read_bytes()).hexdigest()==info['sha256'];assert json.loads((src/'restored.json').read_text())['verified_full_flash']
base=[sys.executable,'-m','esptool','--chip','esp32s31','-p','/dev/ttyUSB0','-b','115200','--before','default-reset','--after','hard-reset']
with (R/'identify.log').open('w') as f:subprocess.run(base+['read-mac'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
assert info['mac'] in (R/'identify.log').read_text().lower()
with (R/'backup-current-verify.log').open('w') as f:subprocess.run(base+['verify-flash','0',str(src/'original.bin')],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
for root in [R/'off',R/'on']:
 dst=root/'s31-rgb/backup';dst.mkdir(exist_ok=True,parents=True)
 for n in ['original.bin','manifest.json']:shutil.copy2(src/n,dst/n)
(R/'backup-proof.json').write_text(json.dumps(dict(current_full_flash_matches_original=True,prior_backup=str(src),**info),indent=2)+'\n')
print('Confirmed MAC and verified complete current flash against restored original',flush=True)
