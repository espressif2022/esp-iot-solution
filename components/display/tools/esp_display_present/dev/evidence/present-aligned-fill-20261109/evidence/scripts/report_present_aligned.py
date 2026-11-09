from pathlib import Path
import json,re,csv,gzip,hashlib,shutil,sys,importlib.util,datetime,html,zipfile,xml.etree.ElementTree as ET
R=Path('/tmp/present-aligned-fill-20261109');C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');D=C/'deliveries';O=D/'present-aligned-fill-20261109';MAIN=D/'present-adapter-five-board-20261109'
preview='--preview' in sys.argv
if preview:O=R/'preview'
O.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('runner','/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/tools/run.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
boards=['s3-rgb','s31-rgb','p4-dsi','box3-spi'];PF={'DOUBLE_FULL','TRIPLE_FULL','DOUBLE_PARTIAL','TRIPLE_PARTIAL'}
if not preview:
 status={**json.loads((R/'completion.json').read_text()),**json.loads((R/'resume-completion.json').read_text())};assert all(status[b] for b in boards),status
base=json.loads((R/'before/comparison-current.json').read_text());rows=json.loads(json.dumps(base));lookup={(r['board'],r['mode'],r['rotation']):r for r in rows};proof=[];raw={};manifests={};grades=0;skips=0
for row in rows:
 for impl in ['present','adapter','present_off']:row[impl+'_cohort']='historical'
for b in boards:
 for impl in ['present','adapter']+(['present_off'] if b=='s3-rgb' else []):
  root=R/'iram-off' if impl=='present_off' else R;native='present' if impl=='present_off' else impl;p=root/b/f'benchmark/{native}'
  if not (p/'manifest.json').exists():
   if preview:continue
   raise RuntimeError('Missing build')
  manifest=json.loads((p/'manifest.json').read_text());manifests[b+'/'+impl]=manifest
  result=m.parse_log((p/'live.log').read_text(errors='replace')) if (p/'live.log').exists() else dict(rows=[],complete=False,errors=[])
  if not preview:
   assert result['complete'] and not result['errors'],(b,impl,result['errors']);assert len(result['rows'])==len(manifest['cases']);assert all(r['scenes']==16 for r in result['rows'] if r['status']=='PASS')
  raw[b+'/'+impl]=result
  for item in result['rows']:
   key=(b,item['mode'],item['rotation']);assert key in lookup,key;row=lookup[key]
   row[impl+'_cohort']='aligned-v1';row[impl+'_status']=item['status'];row[impl+'_note']=item.get('reason','logical50 input allocation aligned')
   for k in ['lvgl_fps','cpu','render','flush','scenes','frames','wait_avg_us','tx_avg_us']:row[impl+'_'+k]=item.get(k,'-')
   for heap in ['internal','psram']:
    for phase in ['before_open','after_open','after_run','after_close']:
     field=heap+('_min_free' if phase=='after_run' else '_free');row[f'{impl}_{heap}_{phase}_free_bytes']=item.get('memory',{}).get(phase,{}).get(field,'-')
   if item['status']=='SKIP':skips+=1;continue
   audit=item.get('buffer_audit',{});assert audit.get('rendered',0)>0 and audit.get('flushes',0)>0,(b,impl,key)
   case=next(c for c in manifest['cases'] if c['name']==item['mode'] and c['rotation']==item['rotation'])
   expected=m.expected_input_bytes(m.BOARDS[b],case,native)
   if expected is not None:assert audit['min_bytes']==expected==audit['max_bytes'],(key,impl,audit)
   for k,v in audit.items():row[impl+'_buffer_'+k]=v
   row[impl+'_effective_fps']=audit['rendered']*1e6/audit['elapsed_us']
   row[impl+'_flushes_per_render']=audit['flushes']/audit['rendered']
   proof.append(dict(board=b,implementation=impl,mode=item['mode'],expected=expected,**audit));grades+=1
  if not preview:
   dst=O/'evidence'/b/impl;dst.mkdir(parents=True,exist_ok=True)
   for name in ['manifest.json','sdkconfig','bench_cases.h','results.json']:shutil.copy2(p/name,dst/name)
   for name in ['live.log','build.log']:(dst/(name+'.gz')).write_bytes(gzip.compress((p/name).read_bytes(),mtime=0))
# Pairwise byte and placement checks only apply to equivalent, freshly measured inputs.
pairs=[]
for row in rows:
 if row['present_cohort']!='aligned-v1' or row['adapter_cohort']!='aligned-v1' or row['adapter_status']!='PASS':continue
 if row['present_status']!='PASS':continue
 for field in ['active_bytes','min_bytes','max_bytes','internal','psram']:assert row['present_buffer_'+field]==row['adapter_buffer_'+field],(row['board'],row['mode'],row['rotation'],field)
 pairs.append([row['board'],'present',row['mode'],row['rotation']])

for row in rows:
 if row['board']=='s3-rgb' and row['present_off_cohort']=='aligned-v1' and row['adapter_cohort']=='aligned-v1':
  for field in ['active_bytes','min_bytes','max_bytes','internal','psram']:assert row['present_off_buffer_'+field]==row['adapter_buffer_'+field]
  pairs.append([row['board'],'present_off',row['mode'],row['rotation']])
if not preview:assert grades==122 and skips==6 and len(pairs)==66,(grades,skips,len(pairs))
scene_rows=[]
scene_keys={'Moving wallpaper':'wallpaper','Multiple labels':'labels','Screen sized text':'text'}
pattern=re.compile(r'SCENE mode=(\S+) rotation=(\d+) name="([^"]+)" fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms')
for tag,result in raw.items():
 b,impl=tag.split('/');root=R/'iram-off' if impl=='present_off' else R;native='present' if impl=='present_off' else impl
 passed={(r['mode'],r['rotation']) for r in result['rows'] if r['status']=='PASS'}
 if not passed:continue
 text=(root/b/f'benchmark/{native}/live.log').read_text(errors='replace')
 for match in pattern.finditer(text):
  mode,rot,name,fps,cpu,render,flush=match.groups();rot=int(rot)
  if (mode,rot) not in passed:continue
  record=dict(board=b,implementation=impl,mode=mode,rotation=rot,name=name,fps=int(fps),cpu=int(cpu),render_ms=int(render),flush_ms=int(flush));scene_rows.append(record)
  if name in scene_keys:lookup[b,mode,rot][impl+'_scene_'+scene_keys[name]+'_fps']=int(fps)
if not preview:assert len(scene_rows)==1952,len(scene_rows)
with (O/'scenes.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=['board','implementation','mode','rotation','name','fps','cpu','render_ms','flush_ms'],lineterminator='\n');w.writeheader();w.writerows(scene_rows)

# Never compare a new score with a historical counterpart during preview or future partial updates.
(O/'comparison-current.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n');(O/'buffer-verification.json').write_text(json.dumps(dict(pairs=pairs,records=proof),indent=2)+'\n');(O/'aligned-raw-results.json').write_text(json.dumps(raw,indent=2)+'\n');(O/'aligned-manifests.json').write_text(json.dumps(manifests,indent=2)+'\n')
fields=list(dict.fromkeys(k for r in rows for k in r))
with (O/'comparison-current.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');w.writeheader();w.writerows(rows)
# Preserve existing layout and all historical rows; label cohorts explicitly.
s=(R/'before/comparison-current.html').read_text()
s=re.sub(r'(<script id="data" type="application/json">).*?(</script>)',lambda x:x[1]+json.dumps(rows,ensure_ascii=False).replace('</','<\\/')+x[2],s,flags=re.S)
s=s.replace('固定源码 b607bb9a1', '生产源码与 d84edb1d0 一致 · 对齐工具 c3bf82364')
s=s.replace('每个有效组合完整16场景','本轮容量对齐更新；历史项另有标记')
s=re.sub(r'<div class="scope">.*?</div>', '<div class="scope"><b>容量对齐重测</b><p>本轮更新三块 RGB/DSI 板的 FULL/PARTIAL 四角度，以及 BOX-3 的单tile GRAM；S3 IRAM 开/关分表。PARTIAL 双方按逻辑宽×50行×RGB565分配；adapter 只在测试构建副本调整该分配表达式，交接代码未改。FULL核对每张整帧容量。NONE不进入RGB/DSI矩阵。</p><p>DIRECT、AUTO、mosaico TE及双tile GRAM保留历史数据，行内明确标记“历史”。TE布局不强行映射为FULL/PARTIAL。本轮每项完整16场景；原CPU100%不作真实负载。<a href="../present-aligned-fill-20261109/README.md">对齐口径与证据</a></p></div>',s,flags=re.S)
s=s.replace('const phases=', "const sceneLabels={wallpaper:'移动壁纸',labels:'多标签',text:'全屏文字'};\nconst phases=")
s=s.replace('<div class="nav">','<style>.cohort{display:block;font-size:11px;color:#60758a}.buf{font-size:12px}.legacy-row{opacity:.7}</style><div class="nav">',1)
s=s.replace(' · present ${s.present.pass} 通过 · adapter ${s.adapter.pass} 通过 / ${s.adapter.skip} 跳过 · 原固件恢复校验通过',' · 本页累计有效成绩（含历史）：present ${s.present.pass} / adapter ${s.adapter.pass}；各次恢复记录见来源')
s=s.replace("本表S31双方XIP关闭。PARTIAL在90°/270°同填50行并非同容量：present为48000B，adapter为80000B。", "本表S31双方XIP仍关闭。本轮PARTIAL已按角度统一容量：0°/180°80000B、90°/270°48000B。历史容量差异另见专项记录。")
s=s.replace("IRAM开／关为相同版本的两次完整24项测试，分为上下两张独立表；当前32KiB指令缓存＋PSRAM XIP。", "IRAM开／关的FULL/PARTIAL已分别重跑16项，DIRECT/AUTO保留历史；当前32KiB指令缓存＋PSRAM XIP。")
s=s.replace("if(impl.startsWith('present')&&typeof r.adapter_lvgl_fps==='number')", "if(impl.startsWith('present')&&typeof r.adapter_lvgl_fps==='number'&&r[impl+'_cohort']===r.adapter_cohort)")
s=s.replace("if(impl.startsWith('present')&&typeof other==='number')", "if(impl.startsWith('present')&&typeof other==='number'&&r[impl+'_cohort']===r.adapter_cohort)")
old='<tr><td>${esc(mode)}</td>${[0,90,180,270].map(angle=>'
new='<tr class="${rs.find(r=>r.mode===mode)?.[variant+\'_cohort\']===\'aligned-v1\'?\'aligned-row\':\'legacy-row\'}"><td>${esc(mode)}<span class="cohort">${rs.find(r=>r.mode===mode)?.[variant+\'_cohort\']===\'aligned-v1\'?\'本轮容量对齐\':\'历史，未重跑\'}</span></td>${[0,90,180,270].map(angle=>'
assert old in s;s=s.replace(old,new)
# Show bytes and memory type without another selector.
needle='<details><summary>展开四时点内存对照'
extra='<details><summary>有效绘制/秒（整段事件计数，非扫描完成率）</summary><div class="wrap"><table class="effective"><thead><tr><th>模式 / 角度</th>${impls.map(i=>`<th>${label(i)}</th>`).join(\'\')}</tr></thead><tbody>${rs.map(r=>`<tr><td>${esc(r.mode)} / ${r.rotation}°</td>${impls.map(i=>`<td>${typeof r[i+\'_effective_fps\']===\'number\'?r[i+\'_effective_fps\'].toFixed(2):\'—（历史或不支持）\'}</td>`).join(\'\')}</tr>`).join(\'\')}</tbody></table></div></details><details open><summary>活跃输入缓冲核对 · bytes · FULL为单张整帧</summary><div class="wrap"><table class="buffers"><thead><tr><th>模式 / 角度</th>${impls.map(i=>`<th>${label(i)}</th>`).join(\'\')}</tr></thead><tbody>${rs.map(r=>`<tr><td>${esc(r.mode)} / ${r.rotation}°</td>${impls.map(i=>`<td class="buf">${typeof r[i+\'_buffer_active_bytes\']===\'number\'?r[i+\'_buffer_active_bytes\']+\' · \'+(r[i+\'_buffer_internal\']?\'内部RAM\':r[i+\'_buffer_psram\']?\'PSRAM\':\'其他\'):\'—（历史或不支持）\'}</td>`).join(\'\')}</tr>`).join(\'\')}</tbody></table></div><p class="muted">只核对活跃绘制源；FULL的descriptor数量、保底像素、旋转画布及全部panel framebuffer仍须分别记账。没有人为额外分配来填平两套架构的总内存差异。</p></details>'
extra_scene='<details><summary>逐场景：整屏移动壁纸／多标签／全屏文字（LVGL分）</summary><div class="wrap"><table class="scene-scores"><thead><tr><th>模式 / 角度</th>${["wallpaper","labels","text"].map(name=>impls.map(i=>`<th>${label(i)} ${sceneLabels[name]}</th>`).join(\'\')).join(\'\')}</tr></thead><tbody>${rs.filter(r=>r[variant+\'_cohort\']===\'aligned-v1\').map(r=>`<tr><td>${esc(r.mode)} / ${r.rotation}°</td>${["wallpaper","labels","text"].map(name=>impls.map(i=>`<td>${r[i+\'_scene_\'+name+\'_fps\']??\'—\'}</td>`).join(\'\')).join(\'\')}</tr>`).join(\'\')}</tbody></table></div><p class="muted">逐场景值仍是LVGL统计，不是扫描完成率；完整16场景见scenes.csv。</p></details>'
extra=extra.replace('<table class="buffers"><thead><tr><th>模式 / 角度</th>${impls.map(i=>`<th>${label(i)}</th>`).join(\'\')}</tr></thead><tbody>${rs.map(r=>`<tr><td>${esc(r.mode)} / ${r.rotation}°</td>${impls.map(i=>`<td class="buf">${typeof r[i+\'_buffer_active_bytes\']===\'number\'?r[i+\'_buffer_active_bytes\']+\' · \'+(r[i+\'_buffer_internal\']?\'内部RAM\':r[i+\'_buffer_psram\']?\'PSRAM\':\'其他\'):\'—（历史或不支持）\'}</td>`).join(\'\')}</tr>`).join(\'\')}</tbody></table>','<table class="buffers"><thead><tr><th>模式</th>${[0,90,180,270].map(a=>impls.map(i=>`<th>${label(i)} ${a}°</th>`).join(\'\')).join(\'\')}</tr></thead><tbody>${modes.map(mode=>`<tr><td>${esc(mode)}</td>${[0,90,180,270].map(a=>{const r=rs.find(x=>x.mode===mode&&x.rotation===a);return impls.map(i=>`<td class="buf">${typeof r[i+\'_buffer_active_bytes\']===\'number\'?r[i+\'_buffer_active_bytes\']+\' · \'+(r[i+\'_buffer_internal\']?\'内部RAM\':r[i+\'_buffer_psram\']?\'PSRAM\':\'其他\'):\'—\'}</td>`).join(\'\')}).join(\'\')}</tr>`).join(\'\')}</tbody></table>')
assert needle in s;s=s.replace(needle,extra_scene+extra+needle)
s=s.replace('<footer>','<footer><p>本轮FULL/PARTIAL与历史完整矩阵顺序不同，且加入相同轻量事件核对；旧新分差不能全部归因于tile容量。主要比较本轮双方的同容量成绩。每项只有一轮，小幅变化不视为统计显著。</p>',1)
if preview:s=s.replace('<h1>','<h1>进行中预览 · ',1)
(O/'comparison.html').write_text(s);(O/'comparison-current.html').write_text(s)
from xlsx_present_aligned import write_xlsx
write_xlsx(O/'comparison-current.xlsx',rows)
if preview:
 print('Preview grades',grades,'pairs',len(pairs),'at',O);raise SystemExit(0)
# Evidence and scope for final publication.
for b in boards:
 src=R/b/'backup';dst=O/'evidence'/b/'restore';dst.mkdir(parents=True,exist_ok=True)
 restored=json.loads((src/'restored.json').read_text());manifest=json.loads((src/'manifest.json').read_text());assert restored['verified_full_flash'] and restored['sha256']==manifest['sha256']
 for n in ['manifest.json','restored.json','backup-reuse-proof.json']:
  if (src/n).exists():shutil.copy2(src/n,dst/n)
 for n in ['restore.log','restore-verify.log']:(dst/(n+'.gz')).write_bytes(gzip.compress((src/n).read_bytes(),mtime=0))
for n in ['scope.json','events.jsonl','resume-events.jsonl','completion.json','resume-completion.json','canonical-validation.json','adapter-allocation-only.patch']:shutil.copy2(R/n,O/n)
scripts=O/'evidence/scripts';scripts.mkdir(parents=True,exist_ok=True)
for n in ['setup_present_aligned.py','run_present_aligned.py','resume_present_aligned.py','report_present_aligned.py','xlsx_present_aligned.py']:shutil.copy2('/tmp/'+n,scripts/n)
for n in ['canonical-host-runner.log','canonical-host-buffers.log']:(O/'evidence'/(n+'.gz')).write_bytes(gzip.compress((R/n).read_bytes(),mtime=0))
summary={'complete':True,'pass_grades':grades,'skipped_cases':skips,'verified_pairs':len(pairs),'boards_newly_tested':boards,'originals_restored':True,'none_scanout_excluded':True,'single_input_tile':True,'historical_modes_retained':['DOUBLE_DIRECT','AUTO','TE','GRAM_T2'],'production_idf_patched':False}
(O/'validation.json').write_text(json.dumps(summary,indent=2)+'\n')
(O/'README.md').write_text('''# FULL/PARTIAL 同容量重测（2026-11-09）

三块RGB/DSI板：DOUBLE_FULL、TRIPLE_FULL、DOUBLE_PARTIAL、TRIPLE_PARTIAL，0/90/180/270四角度；双方均跑完整16场景。S3额外重跑IRAM off，保持一张网页分开两表。BOX-3补充GRAM_T1_INTERNAL/PSRAM，adapter非0°不支持软件旋转，明确跳过。

122项有效成绩、6项跳过；66组双方有效且活跃源容量一致的对照；内存位置为首个flush记录。每个PARTIAL输入池只有一个tile，LVGL软件绘制线程数仍是两边相同的2。RGB/DSI无NONE。mosaico TE、DIRECT/AUTO及GRAM_T2在合并网页中保留历史标记，不宣称本轮重测。

PARTIAL按逻辑宽×50行×RGB565：S3始终48000B；S31 0/180为80000B、90/270为48000B；P4分别102400B/60000B。本轮对齐方向是缩小原adapter的旋转输入容量以匹配present默认值，不是增大present。FULL每张整帧为物理宽×高×2，不随旋转变化。

adapter公开buffer_height以物理宽计数，无法用整数行精确表达P4的60000B。测试只在隔离副本替换PARTIAL分配表达式，framebuffer退役、复制和同步逻辑不变。源码补丁及构建指纹保存。正式工具提交c3bf82364将这项处理限定在构建overlay中，默认logical-50，可选legacy-physical-50复现旧分配；checkout中的adapter和IDF不变。

BUFFER_AUDIT记录活跃源容量、首个flush的内存位置、刷新/非空绘制/flush次数和像素量；每项结束、显示关闭后才打印，没有逐帧日志。容量每次flush都检查。input_tiles字段是测试配置，FULL不使用它表示descriptor或panel buffer数量。FULL的借用、保底存储和旋转画布差异仍计入整机内存，不人为额外分配来填平。

主表帧率是LVGL16场景整数均值，可能包含空刷新；另列非空绘制/秒，仍不是实际扫描完成率。原CPU100%不作为真实负载。内存沿用四时点：打开前剩余、打开后剩余、运行最小剩余、关闭后剩余；KiB显示，CSV/JSON保存精确bytes；剩余越多越好。不同heap低水位不保证同时发生，不等于静态段占用。首项present在open内初始化LVGL，而adapter在bench_init初始化；首项打开前边界不同，关闭后也不是整个框架反初始化。

本轮保留各板原比较配置的XIP与时钟，只隔离分配策略；S31双方XIP关闭。新子矩阵顺序与旧完整矩阵不同，并加入相同轻量事件核对，因此旧新变化不能完全归因于tile大小；只做一轮，没有交错重复的显著性分析。

运行代码以d84edb1d0为基准，与c3bf82364生产源码一致；两者差别是测试工具。P4正式运行先从DOUBLE_FULL开始；用户先前看到的NONE来自11月4日保留的lvgl_present_benchmark原固件。每块板结束恢复运行前原始固件并独立整片校验，因此P4原固件仍可能显示其旧NONE用例，与本轮矩阵区分。

两路UART首次只读备份中断，未写入。随后将当前整片Flash与此前同MAC、已完整恢复校验的备份核对，一致后才复用。失败记录保留；没有使用半份备份。其余两块新备份完整读取并校验。交付不含build/固件二进制；串口通过不替代目视防撕裂验收。

文件：comparison.html为同网页并排对比；comparison-current.json/csv为合并的新旧数据，cohort区分来源；aligned-raw-results.json仅本轮；buffer-verification.json为逐项容量与位置核对；evidence保留日志、指纹和恢复证明。
''')
# Main page is updated after independent file/browser verification, not here.
print('Final report generated:',O,summary)
