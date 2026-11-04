from pathlib import Path
import sys,json,contextlib,os,time,hashlib,serial,esptool
root=Path('/tmp/gram-background-20261104');mode=sys.argv[1]
port='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:F4:0B:34-if00'
backup=root/'backup-s31.bin'
def esp(args,label,cwd=None):
 previous=Path.cwd()
 try:
  if cwd:os.chdir(cwd)
  with (root/(label+'.log')).open('w') as output,contextlib.redirect_stdout(output),contextlib.redirect_stderr(output):
   until=time.monotonic()+20
   while not Path(port).exists() and time.monotonic()<until:time.sleep(.2)
   time.sleep(1)
   esptool.main(['--chip','esp32s31','-p',port,'-b','2000000']+args)
 finally:os.chdir(previous)
def monitor(label):
 with serial.Serial(port,115200,timeout=.25) as device,(root/(label+'-perf.log')).open('wb',buffering=0) as output:
  recent=b'';until=time.monotonic()+180
  while time.monotonic()<until:
   data=device.read(8192)
   if not data:continue
   output.write(data);recent=(recent+data)[-8192:]
   if b'PERF_DONE' in recent:return
   if any(s in recent for s in [b'FATAL',b'Guru Meditation',b'assert failed',b'Stack smashing']):raise RuntimeError('firmware failure')
  raise RuntimeError('monitor timed out')
if mode=='backup':
 if backup.exists():raise RuntimeError('refuse to overwrite fresh backup')
 backup.touch(mode=0o600)
 esp(['read-flash','0','0x1000000',str(backup)],'backup-read')
 assert backup.stat().st_size==16*1024*1024
 esp(['verify-flash','0',str(backup)],'backup-verify')
 print('fresh backup verified',hashlib.sha256(backup.read_bytes()).hexdigest())
elif mode=='verify':
 assert backup.stat().st_size==16*1024*1024
 esp(['verify-flash','0',str(backup)],'backup-verify-retry')
 print('fresh backup independently verified')
else:
 assert backup.stat().st_size==16*1024*1024
 state={}
 try:
  esp(['write-flash','@flash_args'],mode+'-flash',root/('build-'+mode));state['flash']=True
  monitor(mode);state['monitor']=True
 except Exception as error:
  state['error']=repr(error)
 finally:
  try:
   files=json.loads((root/('build-'+mode)/'flasher_args.json').read_text())['flash_files']
   limit=max(int(a,0)+(root/('build-'+mode)/n).stat().st_size for a,n in files.items())
   limit=max(0x100000,(limit+4095)//4096*4096)
   prefix=root/(mode+'-original-prefix.bin');prefix.touch(mode=0o600);prefix.write_bytes(backup.read_bytes()[:limit])
   esp(['write-flash','--flash-size','16MB','0',str(prefix)],mode+'-restore');state['restore']=True;state['restore_bytes']=limit
   esp(['verify-flash','0',str(backup)],mode+'-restore-verify');state['verify']=True
  finally:
   (root/(mode+'-status.json')).write_text(json.dumps(state,indent=2)+'\n')
 print(state)
 if state.get('error'):raise SystemExit(1)
