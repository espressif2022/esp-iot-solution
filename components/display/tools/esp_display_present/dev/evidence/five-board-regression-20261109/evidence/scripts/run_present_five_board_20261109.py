from pathlib import Path
import concurrent.futures as cf
import datetime, hashlib, json, os, shutil, subprocess, sys, threading, traceback
ROOT=Path('/tmp/present-five-board-20261109')
DEV=ROOT/'source/components/display/tools/esp_display_present/dev'
RUNNER=DEV/'tools/run.py'
PERSIST=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/hardware-backups/present-five-board-20261109')
PORTS=json.loads((ROOT/'revision.json').read_text())['ports']
SCANOUT='DOUBLE_FULL,TRIPLE_FULL,DOUBLE_DIRECT,DOUBLE_PARTIAL,TRIPLE_PARTIAL,AUTO'
TE='TE_C1_T1,TE_C1_T2,TE_C2_T1,TE_C2_T2'
ADAPTER_TE='ADAPTER_TE_SYNC_FULL,ADAPTER_TE_ASYNC_FULL,ADAPTER_TE_ASYNC_BOUNCE'
BUILD=threading.Semaphore(2)
LOCK=threading.Lock()
STATE={}
def record(board,stage,**kw):
 with LOCK:
  row=dict(time=datetime.datetime.now(datetime.timezone.utc).isoformat(),board=board,stage=stage,**kw)
  with (ROOT/'events.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
  STATE[board]=row
  (ROOT/'stage.json').write_text(json.dumps(STATE,indent=2)+'\n')
  print(json.dumps(row),flush=True)
def run(board,action,impl='present',suite='benchmark',profiles=None,timeout=4200):
 cmd=[sys.executable,str(RUNNER),action,'--board',board,'--suite',suite,'--implementation',impl,'--port',PORTS[board],'--work-root',str(ROOT),'--timeout',str(timeout)]
 if profiles:cmd+=['--profiles',profiles]
 log=ROOT/f'{board}-{suite}-{impl}-{action}.log'
 record(board,action,implementation=impl,suite=suite,log=str(log))
 with log.open('w') as f:
  p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:
  record(board,'COMMAND_FAILED',action=action,implementation=impl,tail=log.read_text(errors='replace')[-3000:])
  raise RuntimeError(f'{board}/{impl}/{action} failed; see {log}')
def persist_backup(board):
 src=ROOT/board/'backup';info=json.loads((src/'manifest.json').read_text())
 assert info['verified'] and hashlib.sha256((src/'original.bin').read_bytes()).hexdigest()==info['sha256']
 dst=PERSIST/board;dst.mkdir(parents=True,exist_ok=True)
 for name in ('original.bin','manifest.json'):shutil.copy2(src/name,dst/name)
 (dst/'restoration-status.json').write_text(json.dumps(dict(state='backup_verified',**info),indent=2)+'\n')
def persist_restore(board):
 src=ROOT/board/'backup';dst=PERSIST/board
 for name in ('restore.log','restore-verify.log','restored.json'):shutil.copy2(src/name,dst/name)
 info=json.loads((src/'restored.json').read_text());assert info['verified_full_flash']
 (dst/'restoration-status.json').write_text(json.dumps(dict(state='restored_and_verified',**info),indent=2)+'\n')
def board_task(board):
 changed=False;passed=False;restored=False
 try:
  run(board,'backup');persist_backup(board)
  for impl in ('present','adapter'):
   profiles=SCANOUT if board in ('s3-rgb','s31-rgb','p4-dsi') else (TE if impl=='present' else ADAPTER_TE) if board=='s31-qspi' else None
   record(board,'build_wait',implementation=impl)
   with BUILD:run(board,'build',impl,profiles=profiles)
  for impl in ('present','adapter'):
   changed=True;run(board,'flash',impl);run(board,'capture',impl);run(board,'report',impl)
   result=json.loads((ROOT/board/'benchmark'/impl/'results.json').read_text())
   assert result['complete'] and result['matrix_complete'] and result['memory_complete'] and not result['errors']
   assert all(r['scenes']==16 for r in result['rows'] if r['status']=='PASS')
   record(board,'SUITE_PASS',implementation=impl,passed=sum(r['status']=='PASS' for r in result['rows']),skipped=sum(r['status']=='SKIP' for r in result['rows']))
  if board!='s31-qspi':run(board,'compare')
  passed=True
 except BaseException as e:
  record(board,'FAILED',error=repr(e));(ROOT/f'{board}-failure.txt').write_text(traceback.format_exc())
 finally:
  if changed:
   try:run(board,'restore');persist_restore(board);restored=True;record(board,'RESTORE_VERIFIED')
   except BaseException as e:record(board,'RESTORE_FAILED',error=repr(e));(ROOT/f'{board}-restore-failure.txt').write_text(traceback.format_exc())
  record(board,'done',tests_succeeded=passed,firmware_changed=changed,restored=restored)
 return passed and (restored or not changed)
if __name__=='__main__':
 with cf.ThreadPoolExecutor(max_workers=5) as pool:
  futures={pool.submit(board_task,b):b for b in PORTS}
  results={futures[f]:f.result() for f in cf.as_completed(futures)}
 (ROOT/'completion.json').write_text(json.dumps(results,indent=2)+'\n')
 sys.exit(0 if all(results.values()) else 1)
