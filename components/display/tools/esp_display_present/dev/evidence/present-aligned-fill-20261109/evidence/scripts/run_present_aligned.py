from pathlib import Path
import concurrent.futures as cf,subprocess,sys,threading,time,datetime,json,hashlib,shutil,re,signal,traceback
R=Path('/tmp/present-aligned-fill-20261109');PERSIST=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-aligned-fill-20261109')
PORTS=json.loads((R/'scope.json').read_text())['ports'];SCAN='DOUBLE_FULL,TRIPLE_FULL,DOUBLE_PARTIAL,TRIPLE_PARTIAL';GRAM='GRAM_T1_INTERNAL,GRAM_T1_PSRAM';LOCK=threading.Lock();STATE={}
def record(board,stage,**kw):
 with LOCK:
  x=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),board=board,stage=stage,**kw);STATE[board]=x;(R/'stage.json').write_text(json.dumps(STATE,indent=2)+'\n')
  with (R/'events.jsonl').open('a') as f:f.write(json.dumps(x)+'\n')
  print(json.dumps(x),flush=True)
def run(board,action,impl='present',off=False):
 root=R/'iram-off' if off else R;runner=root/'source/components/display/tools/esp_display_present/dev/tools/run.py';profiles=GRAM if board=='box3-spi' else SCAN
 cmd=[sys.executable,str(runner),action,'--board',board,'--suite','benchmark','--implementation',impl,'--port',PORTS[board],'--work-root',str(root),'--profiles',profiles,'--angles','0,90,180,270','--timeout','2200']
 record(board,action,implementation=impl,iram_off=off)
 logfile=root/f'{board}-{action}-{impl}.log'
 with logfile.open('w') as out:
  if action!='capture':subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=1200)
  else:
   live=root/board/f'benchmark/{impl}/live.log';p=subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT);last=None;changed=time.monotonic();passes=0
   try:
    while p.poll() is None:
     time.sleep(5)
     if not live.exists():continue
     s=live.read_text(errors='replace');runs=re.findall(r'RUN mode=.*',s);now=len(runs)
     if now!=last:last=now;changed=time.monotonic();record(board,'running_case',implementation=impl,iram_off=off,current=runs[-1] if runs else '')
     done=re.findall(r'PASS mode=.*',s)
     if len(done)>passes:
      for line in done[passes:]:record(board,'case_pass',implementation=impl,iram_off=off,result=line)
      passes=len(done)
     if 'SUITE_PASS ' not in s and time.monotonic()-changed>210:raise RuntimeError('Case stopped progressing for210s')
    if p.returncode:raise RuntimeError('Capture error: '+logfile.read_text(errors='replace')[-1800:])
   finally:
    if p.poll() is None:p.send_signal(signal.SIGINT);p.wait(timeout=15)
 return root/board/f'benchmark/{impl}'
def persist(board,restore=False):
 src=R/board/'backup';dst=PERSIST/board;dst.mkdir(parents=True,exist_ok=True)
 names=['restore.log','restore-verify.log','restored.json'] if restore else ['original.bin','manifest.json']
 for n in names:shutil.copy2(src/n,dst/n)
 if restore:assert json.loads((dst/'restored.json').read_text())['verified_full_flash']
 else:
  m=json.loads((dst/'manifest.json').read_text());assert m['verified'] and hashlib.sha256((dst/'original.bin').read_bytes()).hexdigest()==m['sha256']
def task(board,builds):
 changed=False;ok=False;restored=False
 try:
  run(board,'backup');persist(board)
  for impl in ['present','adapter']:
   builds[board,impl,False].result();changed=True;run(board,'flash',impl);run(board,'capture',impl);out=run(board,'report',impl)
   x=json.loads((out/'results.json').read_text());assert x['complete'] and x['matrix_complete'] and x['memory_complete'] and not x['errors'];assert all(r['scenes']==16 for r in x['rows'] if r['status']=='PASS')
   s=(out/'live.log').read_text();assert s.count('BUFFER_AUDIT:')==sum(r['status']=='PASS' for r in x['rows']);record(board,'suite_pass',implementation=impl,grades=[r.get('lvgl_fps','-') for r in x['rows']])
  if board=='s3-rgb':
   builds[board,'present',True].result();dst=R/'iram-off'/board/'backup';dst.mkdir(parents=True,exist_ok=True)
   for n in ['original.bin','manifest.json']:shutil.copy2(R/board/'backup'/n,dst/n)
   run(board,'flash',off=True);run(board,'capture',off=True);out=run(board,'report',off=True);x=json.loads((out/'results.json').read_text());assert x['complete'] and x['matrix_complete'] and x['memory_complete'] and not x['errors'] and len(x['rows'])==16
  ok=True
 except BaseException as e:
  record(board,'failed',error=repr(e));(R/f'{board}-failure.txt').write_text(traceback.format_exc())
 finally:
  if changed:
   try:run(board,'restore');persist(board,True);restored=True
   except BaseException as e:record(board,'restore_failed',error=repr(e));(R/f'{board}-restore-failure.txt').write_text(traceback.format_exc())
  record(board,'done',success=ok,restored=restored,firmware_changed=changed)
 return ok and restored
if __name__=='__main__':
 boards=['p4-dsi','s31-rgb','s3-rgb','box3-spi']
 with cf.ThreadPoolExecutor(max_workers=2) as builder,cf.ThreadPoolExecutor(max_workers=4) as hw:
  builds={}
  for impl in ['present','adapter']:
   for b in boards:builds[b,impl,False]=builder.submit(run,b,'build',impl)
  builds['s3-rgb','present',True]=builder.submit(run,'s3-rgb','build','present',True)
  fs={hw.submit(task,b,builds):b for b in boards};done={fs[f]:f.result() for f in cf.as_completed(fs)}
 (R/'completion.json').write_text(json.dumps(done,indent=2)+'\n');raise SystemExit(0 if all(done.values()) else 1)
