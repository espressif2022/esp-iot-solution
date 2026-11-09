from pathlib import Path
from decimal import Decimal,ROUND_HALF_UP
import csv,json,zipfile,xml.etree.ElementTree as ET
P=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-adapter-five-board-20261109')
rows=json.loads((P/'comparison-current.json').read_text());raw=json.loads((P/'raw-results.json').read_text());ns={'x':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
assert len(rows)==120 and len({(r['board'],r['mode'],r['rotation']) for r in rows})==120
phases=['before_open','after_open','after_run','after_close'];cells=0
with (P/'comparison-current.csv').open(encoding='utf-8-sig',newline='') as f:
 csvrows=list(csv.DictReader(f));assert len(csvrows)==120
for r,cr in zip(rows,csvrows):
 for key,value in r.items():assert cr[key]==str(value),(key,cr[key],value)
 for impl in ['present','adapter']:
  entries=[x for x in raw[r['board']][impl]['rows'] if x['mode']==r['mode'] and x['rotation']==r['rotation']]
  assert len(entries)<=1
  item=entries[0] if entries else None
  if item:assert r[impl+'_lvgl_fps']==item.get('lvgl_fps','-')
  for heap in ['internal','psram']:
   for phase in phases:
    key=f'{impl}_{heap}_{phase}_free_bytes';v=r[key]
    expected=item['memory'][phase][heap+('_min_free' if phase=='after_run' else '_free')] if item and item['status']=='PASS' else '-'
    assert v==expected,(r['board'],r['mode'],r['rotation'],key)
    cells+=isinstance(v,int)
def get(c):
 if c.get('t')=='inlineStr':return ''.join(c.itertext())
 v=c.find('x:v',ns);return Decimal(v.text) if v is not None else None
excel=0
with zipfile.ZipFile(P/'comparison-current.xlsx') as z:
 assert z.testzip() is None
 for i,b in enumerate(['s3-rgb','s31-rgb','p4-dsi','s31-qspi','box3-spi'],2):
  root=ET.fromstring(z.read(f'xl/worksheets/sheet{i}.xml'));rr=root.findall('x:sheetData/x:row',ns);selected=[r for r in rows if r['board']==b]
  assert len(rr)==len(selected)*4+1
  for n,r in enumerate(selected):
   for j,phase in enumerate(phases):
    values=[get(c) for c in rr[1+n*4+j].findall('x:c',ns)]
    assert values[0]==r['mode'] and values[1]==r['rotation']
    for index,(heap,impl) in enumerate((h,i) for h in ['internal','psram'] for i in ['present','adapter']):
     value=r[f'{impl}_{heap}_{phase}_free_bytes'];expected=(Decimal(value)/1024).quantize(Decimal('.01'),rounding=ROUND_HALF_UP) if isinstance(value,int) else value
     assert values[5+index]==expected,(b,n,j,index,values[5+index],expected)
     excel+=isinstance(value,int)
result=dict(passed=True,records=len(rows),rawByteCellsChecked=cells,excelMemoryCellsChecked=excel,csvAllFieldsExact=True,allAngles=[0,90,180,270],rgbDsiNoneExcluded=all(r['mode']!='NONE' for r in rows if r['board'] in ['s3-rgb','s31-rgb','p4-dsi']),mosaicoTeOnly=all('TE_' in r['mode'] for r in rows if r['board']=='s31-qspi'))
(P/'file-validation.json').write_text(json.dumps(result,indent=2)+'\n');print(result)
