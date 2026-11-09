from pathlib import Path
import json,statistics,sys,html,re,csv
O=Path(sys.argv[1]);rows=json.loads((O/'comparison-current.json').read_text());out=[]
for b,p,label in [('s3-rgb','present','S3 RGB · IRAM on'),('s3-rgb','present_off','S3 RGB · IRAM off'),('s31-rgb','present','S31 RGB'),('p4-dsi','present','P4 DSI')]:
 for kind in ['FULL','PARTIAL']:
  rs=[r for r in rows if r['board']==b and r['mode'].endswith('_'+kind) and r[p+'_cohort']=='aligned-v1' and r['adapter_cohort']=='aligned-v1']
  if len(rs)!=8:continue
  pm=statistics.mean(r[p+'_lvgl_fps'] for r in rs);am=statistics.mean(r['adapter_lvgl_fps'] for r in rs)
  ep=statistics.mean(r[p+'_effective_fps'] for r in rs);ea=statistics.mean(r['adapter_effective_fps'] for r in rs)
  rec=dict(board=b,label=label,implementation=p,mode_group=kind,case_count=8,present_lvgl_score_mean=pm,adapter_lvgl_score_mean=am,lvgl_delta_percent=(pm/am-1)*100,present_render_per_s_mean=ep,adapter_render_per_s_mean=ea,render_delta_percent=(ep/ea-1)*100)
  for heap in ['internal','psram']:
   v=[r[p+'_'+heap+'_after_open_free_bytes']-r['adapter_'+heap+'_after_open_free_bytes'] for r in rs]
   rec[heap+'_after_open_remaining_delta_min_bytes']=min(v);rec[heap+'_after_open_remaining_delta_max_bytes']=max(v)
  out.append(rec)
(O/'analysis-summary.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
lines=['# 同容量重测结果','', '以下是双缓冲／三缓冲 × 四角度共8项LVGL综合分的均值，非真实扫描FPS。每项包含16场景，只有一轮；小幅分差不视为统计显著。','', '| 板卡 | 组 | present | adapter | present相对差值 | 非空绘制/s present / adapter |','|---|---|---:|---:|---:|---:|']
for r in out:lines.append(f"| {r['label']} | {r['mode_group']} | {r['present_lvgl_score_mean']:.2f} | {r['adapter_lvgl_score_mean']:.2f} | {r['lvgl_delta_percent']:+.1f}% | {r['present_render_per_s_mean']:.2f} / {r['adapter_render_per_s_mean']:.2f} |")
lines+=['','本轮结果不支持“差距全由tile大小引起”：S31、P4的PARTIAL在容量与内存位置一致后仍落后，而FULL的不同缓冲模式表现不同。应继续按逐场景绘制、复制/合成和等待路径分析；本轮不据此宣称已证明具体瓶颈。','', '对齐采用present原本的逻辑宽×50行策略，缩小adapter在90°/270°的输入容量；没有给present加大tile。旧新测试顺序及核对代码不同，因此不能把旧新分差全部解释成tile容量的因果影响。','', 'S3保持原XIP开启，S31保持双方XIP关闭，P4保持原配置；这是各板内部的同配置比较，不能用跨板分差证明某一种芯片机制更优。BOX-3只有0°能双方等价比较，其余角度adapter不支持；mosaico TE保留历史，不混入本轮均值。','']
lines+=['剩余内存差值范围（present − adapter，打开后，KiB，正数表示present剩余更多；范围覆盖该组8项）：','', '| 板卡 | 组 | 内部RAM | PSRAM |','|---|---|---:|---:|']
for r in out:
 vals=[f"{r[heap+'_after_open_remaining_delta_min_bytes']/1024:+.2f} ～ {r[heap+'_after_open_remaining_delta_max_bytes']/1024:+.2f}" for heap in ['internal','psram']]
 lines.append(f"| {r['label']} | {r['mode_group']} | {vals[0]} | {vals[1]} |")
lines+=['','该差值是两个固件在该时点的整堆剩余之差，包含运行存储及固件布局影响，不等于单独组件的动态分配；打开增量还会受到首项LVGL初始化边界不同的影响。容量核对只证明活跃输入一致，未把present的保底存储和旋转画布人为填平。','']
scene_summary=[]
with (O/'scenes.csv').open(encoding='utf-8-sig') as f: scenes=list(csv.DictReader(f))
for board in ['s3-rgb','s31-rgb','p4-dsi']:
 values={}
 for r in scenes:
  if r['board']!=board or not r['mode'].endswith('PARTIAL') or r['implementation'] not in ['present','adapter']:continue
  values.setdefault(r['name'],{}).setdefault(r['implementation'],[]).append(int(r['fps']))
 ranked=[]
 for name,d in values.items():
  if len(d.get('present',[]))!=8 or len(d.get('adapter',[]))!=8:continue
  p=statistics.mean(d['present']);a=statistics.mean(d['adapter']);ranked.append((p-a,name,p,a));scene_summary.append(dict(board=board,group='PARTIAL',scene=name,case_count=8,present_mean=p,adapter_mean=a))
 if ranked:
  lines += ['',f'## {board} PARTIAL逐场景均值','', '| 场景 | present | adapter | 分差 |','|---|---:|---:|---:|']
  for delta,name,p,a in sorted(ranked):lines.append(f'| {name} | {p:.2f} | {a:.2f} | {delta:+.2f} |')
lines+=['','S31差距最明显的并非只限整屏填充：Multiple arcs为24.00/40.00，而Moving wallpaper为12.88/14.12。P4的Multiple ARGB images为46.88/59.50，但Screen sized text为14.38/14.50。它们只定位到场景差异，不能单凭LVGL分数断言是合成、cache或提交中的某一环。','']
(O/'scene-summary.json').write_text(json.dumps(scene_summary,ensure_ascii=False,indent=2)+'\n')
(O/'ANALYSIS.md').write_text('\n'.join(lines))
block='<section id="aligned-overview" class="scope"><h2>本轮同容量结果概览</h2><p>每组为双／三缓冲 × 四角度的8项LVGL综合分均值，不是真实扫描FPS。详项、四时点内存和逐场景分数在各板表内。</p><div class="wrap"><table><thead><tr><th>板卡</th><th>组</th><th>present（差值对adapter）</th><th>adapter</th></tr></thead><tbody>'
for r in out:block+=f"<tr><td>{html.escape(r['label'])}</td><td>{r['mode_group']}</td><td>{r['present_lvgl_score_mean']:.2f} <small>{r['lvgl_delta_percent']:+.1f}%</small></td><td>{r['adapter_lvgl_score_mean']:.2f}</td></tr>"
block+='</tbody></table></div><p>容量一致后，S31、P4的PARTIAL仍有差距；不能全部归因于tile分配。<a href="../present-aligned-fill-20261109/ANALYSIS.md">分析口径与限制</a></p></section>'
for name in ['comparison.html','comparison-current.html']:
 p=O/name;s=p.read_text();s=re.sub(r'<section id="aligned-overview".*?</section>', '', s, flags=re.S);needle='<div class="nav">';assert needle in s;s=s.replace(needle,block+needle,1);p.write_text(s)
print('\n'.join(lines))
