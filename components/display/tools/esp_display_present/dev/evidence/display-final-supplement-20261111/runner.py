from pathlib import Path
import sys,time,json
r=Path(__file__).parent
board=sys.argv[sys.argv.index('--board')+1];diag='--driver-trace' in sys.argv
src='diag-source' if diag else 'te-source' if board=='s31-qspi' else 'source'
sys.path.insert(0,str(r/src/'components/display/tools/esp_display_present/dev/tools'))
import run
original=run.cases_for
def cases(b,angles,profiles=None):
 rows=original(b,angles,profiles)
 if b=='p4-dsi' and not diag:
  done={('DOUBLE_DIRECT',270),('DOUBLE_PARTIAL',90),('DOUBLE_PARTIAL',270),('TRIPLE_PARTIAL',90),('AUTO',270)}
  rows=[x for x in rows if (x['name'],x['rotation']) not in done]
 if b=='s31-qspi':rows=[x for x in rows if x['adapter_supported']]
 return rows
run.cases_for=cases
if sys.argv[1]=='capture':
 if board=='s31-qspi':
  # Flash has just MAC-verified and reset this board. Do not reset USB again.
  run.identify=lambda args:None
 import serial
 Base=serial.Serial
 class ReconnectingSerial(Base):
  _quiet_reopen=False
  def open(self):
   super().open()
  _disconnects=0
  def _update_dtr_state(self):
   if not self._quiet_reopen:super()._update_dtr_state()
  def _update_rts_state(self):
   if not self._quiet_reopen:super()._update_rts_state()
  def read(self,size=1):
   while True:
    try:return super().read(size)
    except (serial.SerialException,OSError) as e:
     with (r/(board+'-link-events.jsonl')).open('a') as f:f.write(json.dumps({'time':time.time(),'event':'disconnect','error':str(e)})+'\n')
     try:super().close()
     except OSError:pass
     self._disconnects+=1
     if self._disconnects>3:raise RuntimeError('Repeated USB loss; interrupted run is not a score')
     self._quiet_reopen=True
     time.sleep(3)
     deadline=time.monotonic()+90
     while time.monotonic()<deadline:
      time.sleep(.5)
      try:
       self.port='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_'+run.BOARDS[board]['mac'].upper()+'-if00'
       super().open();break
      except (serial.SerialException,OSError):pass
     else:raise TimeoutError('USB identity did not return')
 serial.Serial=ReconnectingSerial
run.main()
