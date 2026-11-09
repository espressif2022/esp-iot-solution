from pathlib import Path
import json,html,hashlib,gzip,shutil,csv,datetime
R=Path('/tmp/present-s31-rgb-analysis-20261109');OUT=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-rgb-analysis-20261109')
RUNS=[('baseline','present',True),('baseline','adapter',False),('burst128','present',True),('cache_contract','present',True),('burst_cache','present',True),('baseline','present',False),('burst_cache','present',False),('xip','present',False),('xip','adapter',False),('adapter_matched','adapter',False)]
LABELS={'baseline':'原始 present','burst128':'仅 burst128','cache_contract':'仅缓存维护精简','burst_cache':'burst128＋缓存精简'}
KEYS=[('DOUBLE_PARTIAL',0),('DOUBLE_PARTIAL',90),('TRIPLE_PARTIAL',0),('TRIPLE_PARTIAL',90)]
state=json.loads((R/'stage.json').read_text());assert state['action']=='done' and state['tests_succeeded'] and state['restored'] and state.get('adapter_matched_complete'),state
OUT.mkdir(parents=True,exist_ok=True);rows=[];raw={};manifests={};metrics=[]
for variant,impl,profile in RUNS:
 key=f'{variant}/{impl}'+('-profile' if profile else '')
 path=R/variant/'s31-rgb/benchmark'/(impl+('-profile' if profile else ''))
 result=json.loads((path/'results.json').read_text());manifest=json.loads((path/'manifest.json').read_text())
 assert result['complete'] and result['matrix_complete'] and result['memory_complete'] and not result['errors']
 assert [(r['mode'],r['rotation']) for r in result['rows']]==KEYS
 raw[key]=result;manifests[key]=manifest
 dst=OUT/'evidence'/variant/(impl+('-profile' if profile else ''));dst.mkdir(parents=True,exist_ok=True)
 for name in ['results.json','manifest.json','sdkconfig','bench_cases.h']:shutil.copy2(path/name,dst/name)
 for name in ['live.log','build.log']:
  with (dst/(name+'.gz')).open('wb') as f:f.write(gzip.compress((path/name).read_bytes(),mtime=0))
 for r in result['rows']:
  assert r['status']=='PASS' and r['scenes']==16
  row={'variant':variant,'implementation':impl,'profile':profile,**r};rows.append(row)
  if profile:
   frames=r['stages']['begin']['n']
   for stage,m in r['stages'].items():
    metrics.append({'variant':variant,'mode':r['mode'],'rotation':r['rotation'],'stage':stage,**m,'us_per_frame':m['sum_us']/frames,'span_bytes_per_frame':m['bytes']/frames})
lookup={(r['variant'],r['implementation'],r['profile'],r['mode'],r['rotation']):r for r in rows}
(OUT/'results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n');(OUT/'raw-results.json').write_text(json.dumps(raw,ensure_ascii=False,indent=2)+'\n');(OUT/'manifests.json').write_text(json.dumps(manifests,indent=2)+'\n');(OUT/'metrics.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2)+'\n')
fields=['variant','implementation','profile','mode','rotation','lvgl_fps','render','flush','frames','transport_fps','scenes']
with (OUT/'comparison.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(rows)
with (OUT/'metrics.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(metrics[0]));w.writeheader();w.writerows(metrics)
style='body{font:15px/1.6 system-ui,sans-serif;margin:0;background:#f5f7fa;color:#172b45}main{max-width:1450px;margin:auto;padding:24px}section{background:white;padding:20px;margin:20px 0;border:1px solid #dbe3ec;border-radius:12px}.wrap{overflow:auto}table{border-collapse:collapse;width:100%}th,td{padding:9px 12px;border-bottom:1px solid #dbe3ec;text-align:right;white-space:nowrap}th:first-child,td:first-child{text-align:left}th{background:#edf2f8}.delta{font-size:12px;color:#357455;margin-left:5px}.down{color:#ad4c3e}a{color:#22629d}.muted{color:#64748b}summary{cursor:pointer;font-weight:600}h1{font-size:26px}h2{font-size:21px}'
def grade(r,base=None):
 if r is None:return '—（创建内存不足，未取得成绩）'
 v=r['lvgl_fps'];s=f'<span class="grade">{v}</span>'
 if base:
  d=v-base['lvgl_fps'];s+=f'<span class="delta {"down" if d<0 else ""}">{"⬆️ +"+str(d) if d>0 else "⬇️ "+str(d) if d<0 else "＝ 0"}</span>'
 return s
parts=[f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>S31 RGB 性能定位</title><style>{style}</style><main><h1>S31 RGB · 800×480 · 性能定位</h1><p>2026-11-09 · MAC 30:ed:a0:f3:f7:b8 · 干净 IDF 7b9cc1ac79f8 · 默认XIP关闭，专项同开另列 · 每项完整16场景</p><p>隔离实验，生产代码未修改。相同四项顺序；只列 PARTIAL 0°/90°，不能替代原五板完整矩阵。按用户要求，所有正式对照固定双方单tile；此前双tile尝试已停止，仅保留原始调试记录，不进入对比和结论。带计时和不带计时分表，箭头仅相对各表原始present。单轮整数均值；小差异不代表统计显著变化。</p><p><a href="comparison.csv">FPS CSV</a> · <a href="metrics.csv">分段计时CSV</a> · <a href="README.md">口径与证据</a> · <a href="../present-adapter-five-board-20261109/comparison-current.html">五板原总表</a></p>']
on_p=[lookup['xip','present',False,m,a]['lvgl_fps'] for m,a in KEYS]
on_a=[lookup['xip','adapter',False,m,a]['lvgl_fps'] for m,a in KEYS]
off_p=[lookup['baseline','present',False,m,a]['lvgl_fps'] for m,a in KEYS]
off_a=[lookup['baseline','adapter',False,m,a]['lvgl_fps'] for m,a in KEYS]
combo=[lookup['burst_cache','present',False,m,a]['lvgl_fps'] for m,a in KEYS]
matched=[lookup['adapter_matched','adapter',False,m,a]['lvgl_fps'] for m,a in KEYS]
summary=[f'所有正式对照固定单tile。按双0°、双90°、三0°、三90°顺序：XIP关时present为{off_p}，adapter为{off_a}；双方XIP开时present为{on_p}，adapter为{on_a}。',f'不开XIP时，burst128＋缓存精简的无计时复核为{combo}，与原始present对照。仅修改这些拷贝参数未追回原差距。',f'同开XIP后，adapter再把90°容量改为与present相同的48000B，成绩为{matched}。原始adapter的90°容量为80000B；因此原表同样50行不代表旋转后同容量。','XIP开关同时改变.text和.rodata访问位置；此次没有分别隔离取指、只读数据、I-cache冲突或任务调度成本，不能把现象进一步确定为某一种。897个LVGL对象的可重定位.text字节一致，也不代表最终链接地址和运行缓存行为一致。','本板实际为80MHz DIO Flash、200MHz Octal PSRAM、16KiB指令缓存；XIP开关同时移动代码和只读数据。数据确认了访问路径对成绩的影响，尚未单独量化取指与只读数据的贡献。','本结论限于这块S31 RGB的PARTIAL 0°/90°四项。未重测FULL/DIRECT及180°/270°，不把专项结果替换原五板总表。']
cost={}
for impl in ['present','adapter']:
 off=raw['baseline/'+impl]['suite_memory']['before_init'];on=raw['xip/'+impl]['suite_memory']['before_init']
 cost[impl]={heap+'_heap_reduction_bytes':off[heap+'_total']-on[heap+'_total'] for heap in ['internal','psram']}
summary.insert(-1,f'XIP开并非无内存代价：启动前PSRAM可用堆总量减少present {cost["present"]["psram_heap_reduction_bytes"]/1024:.2f}KiB、adapter {cost["adapter"]["psram_heap_reduction_bytes"]/1024:.2f}KiB；内部堆总量各少约{cost["present"]["internal_heap_reduction_bytes"]/1024:.2f}/{cost["adapter"]["internal_heap_reduction_bytes"]/1024:.2f}KiB。默认配置未改。')
(OUT/'xip-memory-cost.json').write_text(json.dumps(cost,indent=2)+'\n')
parts+=['<section id="findings"><h2>上板结论</h2>'+''.join('<p>'+html.escape(x)+'</p>' for x in summary)+'</section>']
parts+=['<section><h2>隔离实验 · 均带相同计时</h2><div class="wrap"><table id="profile"><thead><tr><th>模式／角度</th>'+''.join(f'<th>{LABELS[v]}</th>' for v in ['baseline','burst128','cache_contract','burst_cache'])+'</tr></thead><tbody>']
for mode,angle in KEYS:
 base=lookup['baseline','present',True,mode,angle]
 parts+=['<tr><td>'+mode+f' / {angle}°</td>'+''.join('<td>'+grade(lookup.get((v,'present',True,mode,angle)),base if v!='baseline' else None)+'</td>' for v in ['baseline','burst128','cache_contract','burst_cache'])+'</tr>']
parts+=['</tbody></table></div></section><section><h2>不带计时复核 · 同板adapter参考</h2><div class="wrap"><table id="control"><thead><tr><th>模式／角度</th><th>原始present</th><th>burst128＋缓存精简</th><th>原始adapter</th></tr></thead><tbody>']
for mode,angle in KEYS:
 base=lookup['baseline','present',False,mode,angle]
 parts+=['<tr><td>'+mode+f' / {angle}°</td>'+''.join('<td>'+grade(lookup[v,i,False,mode,angle],base if v=='burst_cache' else None)+'</td>' for v,i in [('baseline','present'),('burst_cache','present'),('baseline','adapter')])+'</tr>']
parts+=['</tbody></table></div></section><section><h2>XIP 同开对照 · 不带计时</h2><div class="wrap"><table id="xip"><thead><tr><th>模式／角度</th><th>present XIP关</th><th>adapter XIP关</th><th>present XIP开</th><th>adapter XIP开</th></tr></thead><tbody>']
for mode,angle in KEYS:
 parts+=['<tr><td>'+mode+f' / {angle}°</td>'+''.join('<td>'+grade(lookup[v,i,False,mode,angle])+'</td>' for v,i in [('baseline','present'),('baseline','adapter'),('xip','present'),('xip','adapter')])+'</tr>']
parts+=['</tbody></table></div></section><section><h2>旋转缓冲容量对齐 · 双方XIP开、单tile、无计时</h2><p>本表双方均开XIP并使用单tile。原始adapter按物理宽800×50行分配80000B；present在90°按逻辑宽480×50行分配48000B。本组adapter仅在90°将物理行数改30，使容量同为48000B；0°保持80000B。测试顺序相同。</p><div class="wrap"><table id="matched"><thead><tr><th>模式／角度</th><th>present</th><th>adapter原容量</th><th>adapter同容量</th></tr></thead><tbody>']
for mode,angle in KEYS:
 parts+=['<tr><td>'+mode+f' / {angle}°</td>'+''.join('<td>'+grade(lookup[v,i,False,mode,angle])+'</td>' for v,i in [('xip','present'),('xip','adapter'),('adapter_matched','adapter')])+'</tr>']
parts+=['</tbody></table></div></section><section><h2>分段计时</h2><p>各阶段会嵌套、重叠，不能相加作为整帧耗时。缓存bytes是累计维护地址范围，包含重复及非外部地址，不是DDR实际流量。硬件执行包含驱动及等待，acquire包含等tile；begin包含等framebuffer。</p><details><summary>展开每帧平均耗时／维护范围</summary><div class="wrap"><table id="metrics"><thead><tr><th>版本</th><th>模式／角度</th><th>阶段</th><th>调用次数</th><th>累计秒</th><th>每帧ms</th><th>每帧范围MiB</th></tr></thead><tbody>']
for m in metrics:parts.append('<tr>'+''.join(f'<td>{html.escape(str(v))}</td>' for v in [LABELS[m['variant']],m['mode']+f" / {m['rotation']}°",m['stage'],m['n'],f"{m['sum_us']/1e6:.3f}",f"{m['us_per_frame']/1000:.3f}",f"{m['span_bytes_per_frame']/1048576:.3f}"])+'</tr>')
parts+=['</tbody></table></div></details></section><p>原固件已恢复并整片校验。未用串口完成替代目视防撕裂验收。</p></main></html>'];(OUT/'comparison.html').write_text('\n'.join(parts))
lines=['# S31 RGB 性能定位（2026-11-09）','','固定生产源码 b3722b581（运行代码与原五板基线相同）；IDF干净无补丁。默认XIP关闭，专项同开另列；CPU320MHz、PSRAM200MHz。仅PARTIAL四项，顺序为双0/90、三0/90；每项完整16场景。','','## 实验定义','','baseline：原始present。burst128：只设DMA2D burst128，原值默认32。cache_contract：只精简重复维护；DMA2D使用IDF已有源写回和目标写回/失效契约；PPA保留目标预写回，源和完成后维护由驱动契约负责。burst_cache：二者组合。XIP组仅将双方XIP打开；adapter_matched以同开XIP的adapter为参照，仅在90°将buffer_height改30，使容量与present同为48000B；双方始终单tile。所有改动仅在隔离副本，未修改生产或IDF。','','## 带计时结果','','| 模式 | 角度 | 基线 | burst128 | 缓存精简 | 组合 |','|---|---:|---:|---:|---:|---:|']
for mode,angle in KEYS:lines.append('| '+mode+' | '+str(angle)+' | '+' | '.join(str(lookup[v,'present',True,mode,angle]['lvgl_fps']) if (v,'present',True,mode,angle) in lookup else '—（未完成）' for v in ['baseline','burst128','cache_contract','burst_cache'])+' |')
lines+=['','## 不带计时复核','','| 模式 | 角度 | 基线 | 组合 | adapter |','|---|---:|---:|---:|---:|']
for mode,angle in KEYS:lines.append('| '+mode+' | '+str(angle)+' | '+' | '.join(str(lookup[v,i,False,mode,angle]['lvgl_fps']) for v,i in [('baseline','present'),('burst_cache','present'),('baseline','adapter')])+' |')
lines+=['','## XIP同开对照（无计时）','','| 模式 | 角度 | present关 | adapter关 | present开 | adapter开 |','|---|---:|---:|---:|---:|---:|']
for mode,angle in KEYS:lines.append('| '+mode+' | '+str(angle)+' | '+' | '.join(str(lookup[v,i,False,mode,angle]['lvgl_fps']) for v,i in [('baseline','present'),('baseline','adapter'),('xip','present'),('xip','adapter')])+' |')
lines+=['','## 旋转缓冲容量对齐（双方XIP开、单tile、无计时）','','| 模式 | 角度 | present | adapter原容量 | adapter同容量 |','|---|---:|---:|---:|---:|']
for mode,angle in KEYS:lines.append('| '+mode+' | '+str(angle)+' | '+' | '.join(str(lookup[v,i,False,mode,angle]['lvgl_fps']) for v,i in [('xip','present'),('xip','adapter'),('adapter_matched','adapter')])+' |')
lines+=['','按用户要求，所有正式对照固定双方单tile；双tile尝试已停止，仅存原始调试记录，不进入对比和结论。阶段计时嵌套/并行，不可相加。维护范围不是总线流量。控制组没有计时，不能把与profile版本差值都归于生产改动。单轮整数均值，不是重复交错统计检验。独立序列首项adapter与原全矩阵不同，原矩阵数据不覆盖。本轮只完成串口/统计验证，目视未重新验收。','','原固件整片16MiB备份与校验通过；完成后完整恢复并独立校验。备份二进制与build目录不放交付件。']
lines[2:2]=['## 上板结论','',*[line for item in summary for line in (item,'')]]
(OUT/'README.md').write_text('\n'.join(lines)+'\n')
assert len(rows)==40 and all(r['scenes']==16 for r in rows)
(OUT/'validation.json').write_text(json.dumps({'passed':True,'comparison_cases':40,'scenes_per_case':16,'single_tile_all_comparison_cases':all(c['tiles']==1 for m in manifests.values() for c in m['cases']),'excluded_tile2_not_in_comparison':not any(r['variant']=='tile2' for r in rows),'full_firmware_restored':True,'scope':'S31 RGB PARTIAL 0/90 only; single run; optical acceptance not performed'},indent=2)+'\n')
for name in ['scope.json','stage.json','events.jsonl','source-difference-proof.json','sdkconfig-proof.json','lvgl-code-proof.json','boot-xip-proof.json','first-controller-final.json']:shutil.copy2(R/name,OUT/name)
for v in ['burst128','cache_contract','burst_cache','xip','adapter_matched']:shutil.copy2(R/v/'experiment.patch',OUT/(v+'.patch'))
excluded=OUT/'evidence/excluded-tile2';excluded.mkdir(parents=True,exist_ok=True)
shutil.copy2(R/'tile2/experiment.patch',excluded/'experiment.patch')
for name in ['results.json','manifest.json','sdkconfig','bench_cases.h']:shutil.copy2(R/'tile2/s31-rgb/benchmark/present-profile'/name,excluded/name)
(excluded/'live.log.gz').write_bytes(gzip.compress((R/'tile2/s31-rgb/benchmark/present-profile/live.log').read_bytes(),mtime=0))
(excluded/'README.txt').write_text('Stopped at user request. Excluded from all official comparisons and optimization conclusions. Two double-buffer cases completed, triple-buffer creation failed ESP_ERR_NO_MEM; no full-matrix pass.\n')
restore=OUT/'evidence/restore';restore.mkdir(parents=True,exist_ok=True)
for name in ['manifest.json','restored.json']:shutil.copy2(R/'baseline/s31-rgb/backup'/name,restore/name)
for name in ['restore.log','restore-verify.log']:(restore/(name+'.gz')).write_bytes(gzip.compress((R/'baseline/s31-rgb/backup'/name).read_bytes(),mtime=0))
scripts=OUT/'evidence/scripts';scripts.mkdir(parents=True,exist_ok=True)
for source in ['/tmp/run_present_s31_analysis_20261109.py','/tmp/report_present_s31_analysis_20261109.py','/tmp/prebuild_present_s31_variants.py','/tmp/resume_present_s31_analysis_20261109.py','/tmp/prebuild_present_s31_xip.py','/tmp/prebuild_present_s31_controls.py']:shutil.copy2(source,scripts/Path(source).name)
(OUT/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(OUT)}\n' for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
print('Report complete:',OUT,'cases',len(rows))
