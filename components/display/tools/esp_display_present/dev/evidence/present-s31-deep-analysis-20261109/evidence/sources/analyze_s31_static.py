from pathlib import Path
import json,gzip,re,csv,hashlib,subprocess
R=Path('/tmp/present-s31-deep-analysis-20261109'); C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai'); REPO=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR')
F=C/'deliveries/present-adapter-five-board-20261109'
pat=re.compile(r'(\w+)=(?:"([^"]*)"|(\S+))')
def kv(s):return {k:a if a else b for k,a,b in pat.findall(s)}
rows=[]
for board in ['s3-rgb','s31-rgb','p4-dsi','s31-qspi','box3-spi']:
 for impl in ['present','adapter']:
  p=F/f'evidence/{board}/{impl}/live.log.gz'
  if not p.exists():continue
  selected={}
  for line in gzip.decompress(p.read_bytes()).decode(errors='replace').splitlines():
   if 'SCENE mode=' in line:
    d=kv(line);d['board']=board;d['implementation']=impl
    if board in ['s3-rgb','s31-rgb','p4-dsi'] and d['mode']=='NONE':continue
    for k in ['fps','cpu','render','flush']:d[k]=int(re.match(r'\d+',d[k])[0])
    d['rotation']=int(d['rotation']);selected[d['mode'],d['rotation'],d['name']]=d
  rows+=list(selected.values())
with (R/'original-scenes.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=['board','implementation','mode','rotation','name','fps','cpu','render','flush'],lineterminator='\n');w.writeheader();w.writerows(rows)
(R/'original-scenes.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False)+'\n')
configs={}
keys=['CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ','CONFIG_SPIRAM_SPEED','CONFIG_SPIRAM_XIP_FROM_PSRAM','CONFIG_SPIRAM_FETCH_INSTRUCTIONS','CONFIG_SPIRAM_RODATA','CONFIG_ESPTOOLPY_FLASHMODE','CONFIG_ESPTOOLPY_FLASHFREQ','CONFIG_CACHE_L1_ICACHE_SIZE','CONFIG_CACHE_L1_DCACHE_SIZE','CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE','CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT','CONFIG_LV_DEF_REFR_PERIOD','CONFIG_LV_OS_IDLE_PERCENT_CUSTOM']
for board in ['s3-rgb','s31-rgb','p4-dsi','s31-qspi','box3-spi']:
 configs[board]={}
 for impl in ['present','adapter']:
  p=F/f'evidence/{board}/{impl}/sdkconfig'
  if not p.exists():continue
  d={}
  for l in p.read_text().splitlines():
   if l.startswith('# CONFIG_') and l.endswith(' is not set'):d[l[2:-11]]='n'
   elif l.startswith('CONFIG_') and '=' in l:k,v=l.split('=',1);d[k]=v
  configs[board][impl]={k:d.get(k,'not applicable') for k in keys}
proof=[]
base=Path('/tmp/present-s31-rgb-analysis-20261109/baseline/source')
for part in ['esp_display_present','esp_lv_present','esp_lvgl_adapter']:
 prefix=f'components/display/tools/{part}/'
 files=subprocess.check_output(['git','-C',str(REPO),'ls-files',prefix+'src',prefix+'include'],text=True).splitlines()
 for name in files:
  p=base/name
  if not p.is_file():continue
  orig=subprocess.check_output(['git','-C',str(REPO),'show','HEAD:'+name]);proof.append(dict(file=name,baseline_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),head_sha256=hashlib.sha256(orig).hexdigest(),identical=p.read_bytes()==orig))
assert proof and all(p['identical'] for p in proof)
(R/'configuration-audit.json').write_text(json.dumps(configs,indent=2)+'\n')
(R/'production-source-proof.json').write_text(json.dumps(dict(head=subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip(),files=proof,all_identical=True),indent=2)+'\n')
print('scenes',len(rows),'source files',len(proof),'all identical',True)
