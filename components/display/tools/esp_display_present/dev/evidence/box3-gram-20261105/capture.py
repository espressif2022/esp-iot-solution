import json,re,time,sys
from pathlib import Path
import serial
from esptool.reset import HardReset
root=Path(__file__).parent
impl=sys.argv[1]
out=root/impl;out.mkdir(exist_ok=True)
result={'implementation':impl,'board':'ESP-BOX-3 SPI 320x240','mac':'3c:84:27:05:ba:78','port':'/dev/ttyACM0','pass':[],'skip':[],'run':[],'errors':[],'error_lines':[],'boot_count':0,'complete':False,'optical_verification':'Not measured; serial completion and FPS do not prove optical tear-free output.'}
def save():
    p=out/'summary.json.tmp';p.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');p.replace(out/'summary.json')
s=serial.Serial(port=None,baudrate=115200,timeout=.2);s.dtr=False;s.rts=False;s.port=result['port'];s.open();s.reset_input_buffer()
HardReset(s,uses_usb=True)()
start=time.monotonic();last=start;pending=b'';panic=None
save()
with (out/'live.log').open('wb') as log:
    while time.monotonic()-start<2400:
        data=s.read(s.in_waiting or 1)
        if data:
            log.write(data);log.flush();pending+=data
            while b'\n' in pending:
                raw,pending=pending.split(b'\n',1)
                line=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',raw.decode(errors='replace')).strip()
                if 'ESP-ROM:' in line: result['boot_count']+=1
                if re.match(r'E \(',line):result['error_lines'].append(line)
                if ': RUN mode=' in line:result['run'].append(line);print(line,flush=True);save()
                if ': PASS mode=' in line:
                    m=re.search(r'PASS mode=(\w+) te_buffers=(\d+) rotation=(\d+) frames=(\d+) transport_fps=([\d.]+) lvgl_fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms scenes=(\d+)',line)
                    if m:
                        keys=['mode','te_buffers','rotation','frames','transport_fps','lvgl_fps','cpu_percent','render_ms','flush_ms','scenes']
                        vals=[m[1],*map(int,m.group(2,3,4)),float(m[5]),*map(int,m.group(6,7,8,9,10))]
                        result['pass'].append(dict(zip(keys,vals)))
                    else: result['errors'].append('unparsed PASS: '+line)
                    print(line,flush=True);save()
                if ': SKIP mode=' in line:result['skip'].append(line);print(line,flush=True);save()
                if any(x in line for x in ('Guru Meditation','assert failed:','ESP_ERROR_CHECK failed','Stack canary watchpoint','A stack overflow','benchmark timed out',': FAIL mode=')):
                    result['errors'].append(line);panic=panic or time.monotonic();print(line,flush=True);save()
                if 'matrix round=1 complete' in line:result['complete']=True;save()
        if result['complete'] or (panic and time.monotonic()-panic>3):break
        if time.monotonic()-last>=30:
            result['elapsed_s']=round(time.monotonic()-start);save()
            print(json.dumps({'elapsed_s':result['elapsed_s'],'passes':len(result['pass']),'skips':len(result['skip']),'last_run':result['run'][-1:]}),flush=True);last=time.monotonic()
s.close();result['elapsed_s']=round(time.monotonic()-start);save()
print(json.dumps({'complete':result['complete'],'passes':len(result['pass']),'skips':len(result['skip']),'errors':result['errors']}),flush=True)
sys.exit(0 if result['complete'] and not result['errors'] else 1)
