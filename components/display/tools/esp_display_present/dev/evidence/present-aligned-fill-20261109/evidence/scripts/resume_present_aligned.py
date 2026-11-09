from pathlib import Path
import importlib.util,json,subprocess,sys,time,concurrent.futures as cf,datetime,threading,shutil,hashlib
spec=importlib.util.spec_from_file_location('aligned','/tmp/run_present_aligned.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
R=m.R;BASE=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-five-board-20261109');lock=threading.Lock();state={}
def record(board,stage,**kw):
 with lock:
  x=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),board=board,stage=stage,**kw);state[board]=x;(R/'resume-stage.json').write_text(json.dumps(state,indent=2)+'\n')
  with (R/'resume-events.jsonl').open('a') as f:f.write(json.dumps(x)+'\n')
  print(json.dumps(x),flush=True)
m.record=record;original=m.run

def run(board,action,impl='present',off=False):
 if action!='backup':return original(board,action,impl,off)
 record(board,'verify_previous_backup_against_current_flash')
 src=BASE/board;info=json.loads((src/'manifest.json').read_text());assert info['verified'] and hashlib.sha256((src/'original.bin').read_bytes()).hexdigest()==info['sha256']
 target={'s3-rgb':'esp32s3','s31-rgb':'esp32s31'}[board];port=m.PORTS[board]
 cmd=[sys.executable,'-m','esptool','--chip',target,'-p',port,'-b','115200','--before','default-reset','--after','hard-reset']
 with (R/f'{board}-reuse-identify.log').open('w') as f:subprocess.run(cmd+['read-mac'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 assert info['mac'] in (R/f'{board}-reuse-identify.log').read_text().lower()
 with (R/f'{board}-reuse-verify.log').open('w') as f:subprocess.run(cmd+['verify-flash','0',str(src/'original.bin')],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
 dst=R/board/'backup';dst.mkdir(exist_ok=True,parents=True)
 if (dst/'original.bin').exists():shutil.move(dst/'original.bin',dst/'interrupted-read.bin')
 for n in ['original.bin','manifest.json']:shutil.copy2(src/n,dst/n)
 (dst/'backup-reuse-proof.json').write_text(json.dumps(dict(current_full_flash_verified=True,source=str(src),**info),indent=2)+'\n')
m.run=run
class Build:
 def __init__(self,b,impl,off=False):self.b=b;self.impl=impl;self.off=off
 def result(self):
  p=(R/'iram-off' if self.off else R)/self.b/f'benchmark/{self.impl}/manifest.json';end=time.monotonic()+1800
  while not p.exists():
   if time.monotonic()>end:raise RuntimeError('Build did not finish')
   time.sleep(5)
if __name__=='__main__':
 boards=['s31-rgb','s3-rgb'];builds={(b,i,False):Build(b,i) for b in boards for i in ['present','adapter']};builds['s3-rgb','present',True]=Build('s3-rgb','present',True)
 with cf.ThreadPoolExecutor(max_workers=2) as pool:
  fs={pool.submit(m.task,b,builds):b for b in boards};out={fs[f]:f.result() for f in cf.as_completed(fs)}
 (R/'resume-completion.json').write_text(json.dumps(out,indent=2)+'\n');raise SystemExit(0 if all(out.values()) else 1)
