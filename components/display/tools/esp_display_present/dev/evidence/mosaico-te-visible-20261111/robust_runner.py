"""Test-only serial reconnection; never resets the MCU after a link interruption."""
from pathlib import Path
import sys,time,json,datetime
r=Path(__file__).parent
sys.path.insert(0,str(r/'source/components/display/tools/esp_display_present/dev/tools'))
import run
if sys.argv[1]=='capture':
 import serial
 Base=serial.Serial
 class ReconnectingSerial(Base):
  def read(self,size=1):
   while True:
    try:return super().read(size)
    except (serial.SerialException,OSError) as e:
     with (r/'link-events.jsonl').open('a') as log:log.write(json.dumps({'time':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),'event':'disconnect','reason':str(e)})+'\n')
     try:super().close()
     except OSError:pass
     deadline=time.monotonic()+90
     while True:
      if time.monotonic()>deadline:raise TimeoutError('MOSAICO serial did not reconnect in 90s')
      time.sleep(.5)
      try:
       # Stable USB serial identity, not an assumed reallocated ACM number.
       self.port='/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_30:ED:A0:F4:0B:34-if00'
       super().open()
       with (r/'link-events.jsonl').open('a') as log:log.write(json.dumps({'time':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),'event':'reconnect','mcu_reset_requested':False})+'\n')
       break
      except (serial.SerialException,OSError):pass
 serial.Serial=ReconnectingSerial
run.main()
