from pathlib import Path
import json,csv,hashlib,re,gzip
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');D=C/'deliveries';O=D/'present-s31-deep-analysis-20261109'
x=json.loads((O/'results.json').read_text());assert len(x)==8 and all(c['passed'] and len(c['scenes'])==16 for c in x)
assert json.loads((O/'browser-validation.json').read_text())['passed']
with (O/'scenes.csv').open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
assert len(rows)==128
for c in x:
 for s in c['scenes'].values():
  row=next(r for r in rows if r['variant']==c['variant'] and r['implementation']==c['implementation'] and int(r['rotation'])==c['rotation'] and int(r['scene'])==s['scene'])
  for k in ['render_per_s','empty_pct','i_stall_m_per_render','software_draw_ms_per_render','present_workers_ms_per_render']:assert float(row[k])==s[k]
  assert s['cache']['saturated']==0
  assert abs(sum(t['us'] for t in s['tasks'])/(2*s['us'])-1)<0.01
assert json.loads((O/'evidence/restore/restored.json').read_text())['verified_full_flash']
assert json.loads((O/'evidence/restore/manifest.json').read_text())['sha256']=='5f07493de62e062ad2ffd3b49dd38a2dc66146e67e78257dc0e2500decc36ebd'
for p in O.rglob('*'):
 assert 'build' not in p.relative_to(O).parts
 assert p.suffix not in ['.bin','.elf','.map']
 if p.suffix in ['.html','.md']:
  text=p.read_text()
  links=re.findall(r'href="([^"#]+)"',text) if p.suffix=='.html' else re.findall(r'\]\(([^)]+)\)',text)
  for a in links:
   if a.startswith(('https:','http:','#','/')):continue
   assert (p.parent/a.split('#')[0]).exists(),(p,a)
result=dict(passed=True,cases=8,sceneRows=128,csvMatchesJson=True,taskRuntimeBudgetsChecked=128,cacheSaturationSamples=0,noBuildOrFirmware=True,relativeLinksExist=True,verifiedFullFlashRestore=True)
(O/'file-validation.json').write_text(json.dumps(result,indent=2)+'\n')
for folder in ['present-s31-deep-analysis-20261109','present-s31-rgb-analysis-20261109','present-adapter-five-board-20261109']:
 d=D/folder
 (d/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(d)}\n' for p in sorted(d.rglob('*')) if p.is_file() and p!=d/'SHA256SUMS'))
print(result)
