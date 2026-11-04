from pathlib import Path
import json, subprocess,sys,os,time,contextlib,runpy,esptool
root=Path('/tmp/present-isr-join'); board=sys.argv[1]; mode=sys.argv[2] if len(sys.argv)>2 else ''; label=board+('-'+mode if mode else '')
ports={'p4':'/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_E8:F6:0A:E7:A7:8D-if00','s3':'/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_9ce7a3350fb5f011abb434eedc44c5a5-if00-port0'}
if len(sys.argv)>3: ports[board]=sys.argv[3]
py='/home/xuxin/.espressif/python_env/idf6.2_py3.10_env/bin/python'
build=root/('build-'+(mode+'-' if mode else '')+board); backup=Path('/tmp/present-fb-final-20261103/backups')/(board+'.bin')
args=json.loads((build/'flasher_args.json').read_text())['flash_files']
limit=max(int(addr,0)+(build/file).stat().st_size for addr,file in args.items());limit=max(0x100000,(limit+4095)//4096*4096)
prefix=root/(label+'-original-prefix.bin');prefix.write_bytes(backup.read_bytes()[:limit]);prefix.chmod(0o600)
esp=[py,str(root/'esptool_entry.py'),'--chip','esp32'+board,'-p',ports[board],'-b','2000000' if board=='p4' else '921600']
def run(cmd,suffix,cwd=None,timeout=600):
 old_cwd=os.getcwd();old_argv=sys.argv[:]
 with (root/(label+'-'+suffix+'.log')).open('w') as out, contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
  try:
   deadline=time.monotonic()+10
   while not Path(ports[board]).exists() and time.monotonic()<deadline:
    time.sleep(.2)
   if cwd:os.chdir(cwd)
   if cmd[1]==str(root/'esptool_entry.py'):
    esptool.main(cmd[2:])
   else:
    sys.argv=cmd[1:]
    runpy.run_path(cmd[1],run_name='__main__')
   return 0
  except Exception as error:
   print(type(error).__name__,str(error));return 1
  finally:
   os.chdir(old_cwd);sys.argv=old_argv
status={}
try:
 status['flash']=run(esp+['write-flash','@flash_args'],'flash',build)
 if status['flash']==0:
  status['monitor']=run([py,'/tmp/present-perf-20261103/monitor.py',ports[board],str(root/(label+'-perf.log'))],'monitor',timeout=120)
finally:
 status['restore_prefix']=run(esp+['write-flash','--flash-size','16MB','0',str(prefix)],'restore-prefix')
 status['verify']=run(esp+['verify-flash','0',str(backup)],'restored-verify')
 if status['restore_prefix'] or status['verify']:
  status['restore_full']=run(esp+['write-flash','--flash-size','16MB','0',str(backup)],'restore-full')
  status['verify']=run(esp+['verify-flash','0',str(backup)],'restored-verify-retry')
 (root/(label+'-status.json')).write_text(json.dumps(status,indent=2))
 print(status)
