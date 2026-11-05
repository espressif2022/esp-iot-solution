import json,re,time,hashlib,sys
from pathlib import Path
import serial
out=Path('/tmp/present-direct-fix-board-20261105');out.mkdir(exist_ok=True)
port='/dev/ttyUSB0'
result={'board':'ESP32-S3 R16 GC9503 RGB 480x480','mac':'90:e5:b1:d0:a9:d8','port':port,'date':'2026-11-05','pass':[],'skip':[],'run':[],'errors':[],'complete':False,'optical_verification':'Not measured: serial benchmark does not establish absence of visible tearing.'}
def save():
 tmp=out/'summary.json.tmp';tmp.write_text(json.dumps(result,ensure_ascii=False,indent=2));tmp.replace(out/'summary.json')
s=serial.Serial(port=None,baudrate=115200,timeout=0.2);s.dtr=False;s.rts=False;s.port=port;s.open()
s.reset_input_buffer();s.rts=True;time.sleep(.2);s.rts=False
start=time.monotonic();last_heartbeat=start;pending=b'';panic_at=None
save()
with (out/'s3-rgb-live.log').open('wb') as log:
 while time.monotonic()-start<4500:
  data=s.read(s.in_waiting or 1)
  if data:
   log.write(data);log.flush();pending+=data
   while b'\n' in pending:
    raw,pending=pending.split(b'\n',1)
    line=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',raw.decode('utf-8',errors='replace')).strip()
    if 'lvgl_bench: RUN ' in line:
     result['run'].append(line);print(line,flush=True);save()
    if 'lvgl_bench: PASS ' in line:
     match=re.search(r'PASS mode=(\w+) te_buffers=(\d+) rotation=(\d+) frames=(\d+) transport_fps=([\d.]+) lvgl_fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms scenes=(\d+)',line)
     if match:
      keys=['mode','te_buffers','rotation','frames','transport_fps','lvgl_fps','cpu_percent','render_ms','flush_ms','scenes']
      vals=[match[1],*map(int,match.group(2,3,4)),float(match[5]),*map(int,match.group(6,7,8,9,10))]
      result['pass'].append(dict(zip(keys,vals)))
     else:result['errors'].append('unparsed PASS: '+line)
     print(line,flush=True);save()
    if 'lvgl_bench: SKIP ' in line:
     result['skip'].append(line);print(line,flush=True);save()
    if any(x in line for x in ('Guru Meditation','assert failed:','ESP_ERROR_CHECK failed','Stack canary watchpoint','A stack overflow','benchmark timed out')):
     result['errors'].append(line);panic_at=panic_at or time.monotonic();print(line,flush=True);save()
    if 'lvgl_bench: matrix round=1 complete' in line:
     result['complete']=True;save()
  if result['complete'] or (panic_at and time.monotonic()-panic_at>3):break
  if time.monotonic()-last_heartbeat>=30:
   print(json.dumps({'elapsed_s':round(time.monotonic()-start),'passes':len(result['pass']),'skips':len(result['skip']),'last_run':result['run'][-1:]},ensure_ascii=False),flush=True);last_heartbeat=time.monotonic()
s.close();result['elapsed_s']=round(time.monotonic()-start);save()
print(json.dumps({'complete':result['complete'],'passes':len(result['pass']),'skips':len(result['skip']),'errors':result['errors']}),flush=True)
sys.exit(0 if result['complete'] and not result['errors'] else 1)
