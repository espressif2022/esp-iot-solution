from pathlib import Path
import json,csv,zipfile,hashlib
from xml.etree import ElementTree as ET
b=Path('/tmp/present-all-modes-20261107/report');rows=json.loads((b/'all-results.json').read_text());raw=json.loads((b/'raw-memory-phases.json').read_text())
assert len(rows)==92 and len({(z['board'],z['mode'],z['rotation']) for z in rows})==92
lookup={(z['board'],z['implementation'],z['mode'],z['rotation']):z for z in raw}
for z in rows:
 for impl in ['present','adapter']:
  a=lookup.get((z['board'],impl,z['mode'],z['rotation']))
  if not z['adapter_supported'] and impl=='adapter':assert z[impl+'_lvgl_fps']=='-';continue
  assert a and z[impl+'_status']==a['status']
  if a['status']=='PASS':assert z[impl+'_lvgl_fps']==a['lvgl_fps'] and a['scenes']==16
  else:assert z[impl+'_lvgl_fps']=='FAIL'
  m=a.get('memory',{})
  for heap in ['internal','psram']:
   if 'after_open' in m:
    total=m['after_open'][heap+'_total'];free=m['after_open'][heap+'_free']
    assert 0<=free<=total
    assert z[impl+'_'+heap+'_open_used_bytes']==total-free
    assert z[impl+'_'+heap+'_open_free_bytes']==free
   if 'after_run' in m:assert z[impl+'_'+heap+'_run_min_free_bytes']==m['after_run'][heap+'_min_free']
with (b/'all-results-bytes.csv').open(encoding='utf-8-sig') as f:assert len(list(csv.DictReader(f)))==92
with (b/'comparison-KiB.csv').open(encoding='utf-8-sig') as f:
 table=list(csv.reader(f));assert len(table)==93 and all(len(z)==18 for z in table)
with zipfile.ZipFile(b/'present-adapter-20261107.xlsx') as z:
 assert z.testzip() is None
 for name in z.namelist():
  if name.endswith(('.xml','.rels')):ET.fromstring(z.read(name))
 ns={'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
 expected=[None,29,29,21,17,93]
 for i,count in enumerate(expected,1):
  doc=ET.fromstring(z.read(f'xl/worksheets/sheet{i}.xml'))
  if count:assert len(doc.findall('s:sheetData/s:row',ns))==count
print('92 rows: raw FPS and memory recomputation, CSV counts, XLSX ZIP/XML/sheet counts passed')
