from pathlib import Path
import sys,json,csv,re,hashlib,zipfile,xml.etree.ElementTree as E
O=Path(sys.argv[1]);rows=json.loads((O/'comparison-current.json').read_text());checks={}
with (O/'comparison-current.csv').open(encoding='utf-8-sig',newline='') as f:
 cr=list(csv.DictReader(f))
assert len(rows)==len(cr)==120
for r,c in zip(rows,cr):
 for k,v in r.items():assert c[k]==str(v),(r['board'],k,v,c[k])
checks['csv_exact']=len(cr)
html=(O/'comparison-current.html').read_text();embedded=json.loads(re.search(r'<script id="data" type="application/json">(.*?)</script>',html,re.S)[1].replace('<\\/','</'))
assert embedded==rows;checks['html_exact']=True
ns={'m':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
def cells(data):
 root=E.fromstring(data);out=[]
 for row in root.findall('m:sheetData/m:row',ns):
  vals=[]
  for c in row.findall('m:c',ns):
   if c.get('t')=='inlineStr':vals.append(c.find('m:is/m:t',ns).text or '')
   else:
    t=c.find('m:v',ns).text;v=float(t);vals.append(int(v) if v.is_integer() else v)
  out.append(vals)
 return out
with zipfile.ZipFile(O/'comparison-current.xlsx') as z:
 assert z.testzip() is None
 raw=cells(z.read('xl/worksheets/sheet8.xml'));fields=raw.pop(0);assert len(raw)==120
 for r,vals in zip(rows,raw):
  for k,v in zip(fields,vals):assert v==r.get(k,'-'),(k,v,r.get(k))
 # Workbook has 6 board sheets, all 4 memory phases; check exact rounding.
 from decimal import Decimal,ROUND_HALF_UP
 specs=[('s3-rgb','present'),('s3-rgb','present_off'),('s31-rgb','present'),('p4-dsi','present'),('s31-qspi','present'),('box3-spi','present')]
 n=0
 for sheetno,(b,p) in enumerate(specs,2):
  vals=cells(z.read(f'xl/worksheets/sheet{sheetno}.xml'))[1:];expected=[]
  for r in rows:
   if r['board']!=b:continue
   for phase in ['before_open','after_open','after_run','after_close']:
    mem=[r.get(i+'_'+heap+'_'+phase+'_free_bytes','-') for heap in ['internal','psram'] for i in [p,'adapter']]
    mem=[float((Decimal(v)/1024).quantize(Decimal('.01'),rounding=ROUND_HALF_UP)) if isinstance(v,int) else v for v in mem]
    expected.append([r['mode'],r['rotation'],r[p+'_cohort'],r[p+'_lvgl_fps'],r['adapter_lvgl_fps'],r.get(p+'_effective_fps','-'),r.get('adapter_effective_fps','-'),r.get(p+'_buffer_active_bytes','-'),r.get('adapter_buffer_active_bytes','-'),phase,*mem])
  assert vals==expected,(sheetno,b);n+=len(vals)
checks['xlsx_raw_exact']=120;checks['xlsx_memory_phase_rows']=n
raw=json.loads((O/'aligned-raw-results.json').read_text());man=json.loads((O/'aligned-manifests.json').read_text());audits=json.loads((O/'buffer-verification.json').read_text());assert len(audits['pairs'])==66
passes=skips=0
for tag,result in raw.items():
 assert result['complete'] and not result['errors'],tag
 for r in result['rows']:
  assert r['rotation'] in [0,90,180,270]
  assert (r['mode'] in ['GRAM_T1_INTERNAL','GRAM_T1_PSRAM'] if tag.startswith('box3-spi/') else r['mode'] in ['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_PARTIAL','TRIPLE_PARTIAL'])
  if r['status']=='SKIP':skips+=1;continue
  passes+=1;assert r['scenes']==16
  a=r['buffer_audit'];assert a['active_bytes']==a['min_bytes']==a['max_bytes'] and a['input_tiles']==1
  if a['expected_bytes']:assert a['active_bytes']==a['expected_bytes']
 b,impl=tag.split('/');p=O/'evidence'/b/impl
 assert json.loads((p/'manifest.json').read_text())==man[tag]
 for f in ['sdkconfig','bench_cases.h','live.log.gz','build.log.gz']:assert (p/f).is_file()
assert (passes,skips)==(122,6)
for b in ['s3-rgb','s31-rgb','p4-dsi','box3-spi']:
 assert man[b+'/present']['comparable_sdkconfig_sha256']==man[b+'/adapter']['comparable_sdkconfig_sha256']
 p=O/'evidence'/b/'restore';a=json.loads((p/'manifest.json').read_text());r=json.loads((p/'restored.json').read_text());assert r['verified_full_flash'] and r['sha256']==a['sha256']
checks.update(pass_grades=passes,skips=skips,verified_pairs=66,restored_boards=4,matching_comparable_sdkconfig=4)
with (O/'scenes.csv').open(encoding='utf-8-sig') as f:assert len(list(csv.DictReader(f)))==1952
for f in O.rglob('*'):
 if f.is_file():assert f.suffix not in ['.bin','.elf','.o','.a'] and 'build' not in f.relative_to(O).parts,f
checks.update(scene_rows=1952,no_build_or_firmware=True)
(O/'file-validation.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks))
