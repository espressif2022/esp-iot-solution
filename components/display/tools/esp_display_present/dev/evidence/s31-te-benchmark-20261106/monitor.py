from pathlib import Path
import serial,time,re,json
from esptool.reset import HardReset
r=Path(__file__).parent
s=serial.Serial(port=None,baudrate=115200,timeout=.2);s.dtr=False;s.rts=False;s.port='/dev/ttyACM0';s.open();s.reset_input_buffer();HardReset(s,uses_usb=True)()
a=time.monotonic();last=a;pending=b'';passes=[];meters=[];errors=[];complete=False
with (r/'live.log').open('wb') as f:
 while time.monotonic()-a<1400:
  d=s.read(s.in_waiting or 1)
  if d:
   f.write(d);f.flush();pending+=d
   while b'\n' in pending:
    raw,pending=pending.split(b'\n',1);line=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',raw.decode(errors='replace')).strip()
    if 'PASS mode=' in line:passes.append(line)
    if 'TE_METER:' in line:meters.append(line)
    if any(x in line for x in ['Guru Meditation','assert failed','ESP_ERROR_CHECK failed','Stack canary','benchmark timed out']) or (line.startswith('E (') and 'GPIO isr service already installed' not in line):errors.append(line)
    if any(x in line for x in ['RUN mode=','PASS mode=','TE_METER:','TE schedule=','single-run matrix complete']) or line.startswith(('E (','W (')):print(line,flush=True)
    if 'single-run matrix complete' in line:complete=True
  if complete or errors:break
  if time.monotonic()-last>20:print(json.dumps({'elapsed_s':round(time.monotonic()-a),'cases':len(passes),'meters':len(meters)}),flush=True);last=time.monotonic()
s.close();out={'complete':complete,'elapsed_s':round(time.monotonic()-a),'passes':passes,'meters':meters,'errors':errors};(r/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out),flush=True)
