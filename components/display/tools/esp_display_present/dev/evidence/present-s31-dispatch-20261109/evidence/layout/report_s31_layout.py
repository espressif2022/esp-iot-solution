from pathlib import Path
import json,shutil,gzip,html,subprocess,sys
R=Path('/tmp/present-s31-layout-20261109');O=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-dispatch-20261109')
assert json.loads((R/'completion.json').read_text())==dict(success=True,restored=True,firmware_changed=True)
subprocess.run([sys.executable,'/tmp/summarize_s31_layout.py'],check=True,capture_output=True)
d=json.loads((R/'summary.json').read_text());assert len(d)==4 and all(x['cases']==2 and x['pixels_per_render']==384000 and x['flushes_per_render']==10 and x['instruction_conflicts']==0 for x in d)
p=O/'evidence/layout';p.mkdir(parents=True,exist_ok=True)
for n in ['summary.json','proof.json','addresses.json','scope.json','link-only.patch','completion.json','identify.log','verify-original.log','controller.log']:
 shutil.copy2(R/n,p/n)
for tag in ['base-before','adapter','early','base-after']:
 src=R/f'{tag}.log';(p/f'{tag}.log.gz').write_bytes(gzip.compress(src.read_bytes(),mtime=0))
for name,impl in [('baseline','present'),('baseline','adapter'),('early','present')]:
 src=R/name/f's31-rgb/benchmark/{impl}';dst=p/name/impl;dst.mkdir(parents=True,exist_ok=True)
 for n in ['manifest.json','sdkconfig','bench_cases.h']:shutil.copy2(src/n,dst/n)
 (dst/'build.log.gz').write_bytes(gzip.compress((src/'build.log').read_bytes(),mtime=0))
 src=R/name/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark'
 for n in ['CMakeLists.txt','main/main.c']:
  out=dst/'source'/n;out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src/n,out)
for n in ['restore.log','restore-verify.log','restored.json','manifest.json']:
 dst=p/'restore';dst.mkdir(exist_ok=True);shutil.copy2(R/'baseline/s31-rgb/backup'/n,dst/n)
shutil.copy2(R/'lvgl/demos/benchmark/lv_demo_benchmark.c',p/'lv_demo_benchmark.c')
for n in ['setup_s31_layout.py','build_s31_layout.py','run_s31_layout.py','proof_s31_layout.py','summarize_s31_layout.py','report_s31_layout.py']:
 shutil.copy2('/tmp/'+n,p/n)
before=d[0];a=d[1];early=d[2];after=d[3]
# Compare the intervention with pooled baseline runs bracketing it.
base_render=before['raw']['render']+after['raw']['render'];base_us=before['raw']['us']+after['raw']['us'];base_fills=sum(x['raw']['if0']+x['raw']['if1'] for x in [before,after])/base_render
base_stalls=sum(x['raw']['is0']+x['raw']['is1'] for x in [before,after])/base_render
result=dict(baseline_render_per_s=base_render*1e6/base_us,baseline_fills_per_render=base_fills,baseline_stalls_per_render=base_stalls,early_render_change_percent=(early['render_per_s']/(base_render*1e6/base_us)-1)*100,early_fill_change_percent=(early['instruction_fills_per_render']/base_fills-1)*100,early_stall_change_percent=(early['instruction_stalls_per_render']/base_stalls-1)*100)
(p/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
labels={'base-before':'present 原排列 · 前测','adapter':'adapter','early':'present 提前提取混合函数','base-after':'present 原排列 · 后测'}
rows=''.join('<tr>'+''.join('<td>'+v+'</td>' for v in [labels[x['tag']],f"{x['render_per_s']:.2f}",f"{x['instruction_accesses_per_render']/1e6:.2f}",f"{x['instruction_fills_per_render']:.0f}",f"{x['instruction_stalls_per_render']/1e6:.2f}",f"{x['stall_per_fill']:.2f}",str(x['instruction_conflicts'])])+'</tr>' for x in d)
text=f'''<!doctype html><meta charset="utf-8"><title>S31 链接布局定点诊断</title><style>body{{font:16px/1.7 system-ui;max-width:1150px;margin:40px auto;padding:0 20px;color:#233249}}table{{border-collapse:collapse;width:100%}}td,th{{padding:10px;border:1px solid #ccd5df;text-align:right}}td:first-child,th:first-child{{text-align:left}}code{{background:#edf2f7}}</style><h1>S31：链接布局对取指停顿的影响</h1><p><a href="comparison.html">返回 24 项缓存与派发对照</a></p><p>这是额外的定点实验，8 次运行，不替换正式跑分。只保留全屏文字场景，每次 18 秒、前 1 秒预热，XIP 关闭、0°、DOUBLE_PARTIAL、单 tile 80000B、双方 bounce 10 行。测量整机两个 I-bus，不是仅 LVGL 函数。</p><p>顺序：present 原排列 → adapter → present 调整排列 → present 原排列复测；每组重复两次。两份 present 的源代码、SDK 配置、任务和缓存配置相同，只增加两个链接器 <code>-u</code> 参数，提前提取 RGB565/RGB888 混合函数目标文件。139 个 LVGL 源文件在链接前的目标代码一致；链接器会随地址变化重新缩短部分调用指令，最终机器码不保证逐字节相同。链接地址变化影响多处函数，不能据此单独锁定某个函数或某组缓存冲突。</p><table><thead><tr><th>组合</th><th>非空渲染/s</th><th>I 访问 M/帧</th><th>line_fills/帧</th><th>I 停顿 M/帧</th><th>停顿/补入</th><th>请求冲突</th></tr></thead><tbody>{rows}</tbody></table><p>相对前后测合并基线：调整排列后的吞吐 {result['early_render_change_percent']:+.2f}%，补入次数 {result['early_fill_change_percent']:+.2f}%，停顿 {result['early_stall_change_percent']:+.2f}%。</p><p>计数口径：line_fills 是缓存行补入次数；stall_events 是等待期间重复累计的相对指标；请求冲突计数不是缓存组映射冲突。<a href="https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html">IDF 官方说明</a>。</p><p>完整原固件已恢复并独立整片校验。仓库生产源码、IDF 未修改。<a href="evidence/layout/summary.json">原始计数汇总</a> · <a href="evidence/layout/proof.json">相同代码和配置证明</a> · <a href="evidence/layout/addresses.json">实际函数地址</a> · <a href="evidence/layout/link-only.patch">唯一链接变更</a></p>'''
(O/'layout.html').write_text(text)
f=O/'comparison.html';s=f.read_text();anchor='</body>';note='<p><a href="layout.html">继续定位：链接提取顺序定点实验（8 次运行）</a></p>'
if 'href="layout.html"' not in s:s=s.replace(anchor,note+anchor) if anchor in s else s+note
f.write_text(s)
f=O/'README.md';s=f.read_text()
if 'layout.html' not in s:s+='\n[链接布局定点实验](layout.html)：额外 8 次 18 秒全屏文字运行，只改变 present 两个链接符号的提前提取，包含前后基线和 adapter 对照，保持单 tile 和双方 10 行 bounce；不替换正式矩阵。\n'
f.write_text(s)
print(json.dumps(result,indent=2))
