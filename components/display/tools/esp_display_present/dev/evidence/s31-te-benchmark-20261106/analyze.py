from pathlib import Path
import re,json,csv
r=Path(__file__).parent
s=(r/'live.log').read_text(errors='replace');rows=[];current={}
for line in s.splitlines():
 if 'RUN mode=' in line:
  m=re.search(r'RUN mode=(\S+).*rotation=(\d+)',line)
  if m:current={'mode':m[1],'angle':int(m[2])}
 if 'TE_METER:' in line:
  for k,v in re.findall(r'(admitted|wait_avg_us|wait_max_us|immediate|polls|tx_n|tx_avg_us|tx_max_us)=(\d+)',line):current[k]=int(v)
  h=re.search(r'hist_lt_1_4_8_12_17_34_inf_ms=([\d,]+)',line)
  if h:current['wait_histogram_counts']=list(map(int,h[1].split(',')))
 if 'PASS mode=' in line:
  for k,v in re.findall(r'(frames|transport_fps|lvgl_fps|cpu|render|flush|scenes)=([\d.]+)',line):current[k]=float(v) if '.' in v else int(v)
  rows.append(current.copy())
(r/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
if rows:
 keys=['mode','angle','admitted','wait_avg_us','wait_max_us','immediate','tx_n','tx_avg_us','tx_max_us','frames','transport_fps','lvgl_fps','cpu','render','flush','scenes']
 with (r/'results.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=keys,extrasaction='ignore');w.writeheader();w.writerows(rows)
 print(json.dumps(rows,indent=2))
