from pathlib import Path
import json,hashlib,gzip,shutil,subprocess,sys,html
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');O=C/'deliveries/present-s31-dispatch-20261109';S=Path('/tmp/present-s31-stable-20261109');A=Path('/tmp/present-s31-align-20261109');F=Path('/tmp/present-s31-font-iram-20261109');E=O/'evidence/root-cause'
assert json.loads((S/'sampling-completion.json').read_text())['success'] and json.loads((S/'sampling-completion.json').read_text())['restored']
assert json.loads((A/'completion.json').read_text())['success']
assert json.loads((F/'completion.json').read_text())==dict(success=True,restored=True,firmware_changed=True)
subprocess.run([sys.executable,'/tmp/summarize_s31_targeted.py'],check=True,capture_output=True)
subprocess.run(['/home/xuxin/.espressif/python_env/idf6.2_py3.10_env/bin/python','/tmp/parse_s31_stable.py'],check=True,capture_output=True)
data=json.loads(Path('/tmp/s31-targeted-results.json').read_text());assert len(data)==15 and all(len(x['cases'])==2 for x in data);rows={x['tag']:x for x in data}
E.mkdir(exist_ok=True,parents=True);shutil.copy2('/tmp/s31-targeted-results.json',E/'targeted-results.json');shutil.copy2(S/'pc-results.json',E/'pc-results.json');shutil.copy2(S/'pc-function-bounds.json',E/'pc-function-bounds.json')
for root,label,names in [(S,'layout-and-pc',['scope.json','layout-proof.json','constructor-proof.json','padsample-ready.json','pad-normalization.json','loop-layout-proof.json','sampling-completion.json','sampling-controller.log','v2-identify.log','v2-verify-original.log']), (A,'alignment32',['scope.json','ready-builds.json','completion.json','alignment64-rejected.json','controller.log']), (F,'font-iram',['scope.json','ready-builds.json','footprint-proof.json','completion.json','controller.log','identify.log','verify-original.log'])]:
 dst=E/label;dst.mkdir(exist_ok=True,parents=True)
 for n in names:shutil.copy2(root/n,dst/n)
for x in data:
 (E/(x['tag']+'.log.gz')).write_bytes(gzip.compress((Path(x['root'])/(x['tag']+'.log')).read_bytes(),mtime=0))
for p in S.glob('*-font-bitmap.asm'):shutil.copy2(p,E/p.name)
# Archive exact input sources per image, including the generated per-image watch address.
images=[(S,'baseline','present','pc-present'),(S,'baseline','adapter','pc-adapter'),(S,'early','present','pc-early'),(S,'padsample','present','pc-pad24'),(S,'pad24','present','plain-pad24'),(A,'baseline','present','aligned-present'),(A,'baseline','adapter','aligned-adapter'),(A,'pad24','present','aligned-pad24'),(F,'baseline','present','font-present'),(F,'baseline','adapter','font-adapter'),(F,'pad24','present','font-pad24')]
source_checks=[]
for root,name,impl,label in images:
 src=root/name/f's31-rgb/benchmark/{impl}';dst=E/'images'/label;dst.mkdir(exist_ok=True,parents=True);dev=root/name/'source/components/display/tools/esp_display_present/dev';manifest=json.loads((src/'manifest.json').read_text())
 for n in ['manifest.json','sdkconfig','bench_cases.h']:shutil.copy2(src/n,dst/n)
 (dst/'build.log.gz').write_bytes(gzip.compress((src/'build.log').read_bytes(),mtime=0))
 for key,sha in manifest['test_sources_sha256'].items():
  p=dev/key
  if key.endswith('/pc_address.h') and (src/'pc_address.h').exists():p=src/'pc_address.h'
  assert hashlib.sha256(p.read_bytes()).hexdigest()==sha,(label,key)
  out=dst/'test-inputs'/key;out.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(p,out)
 # Older runner manifests do not enumerate .S/.lf; keep and checksum them explicitly.
 for suffix in ['.S','.lf']:
  for p in (dev/'test_apps/benchmark').rglob('*'+suffix):
   out=dst/'test-inputs'/p.relative_to(dev);out.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(p,out)
 source_checks.append(dict(image=label,source_hashes_verified=len(manifest['test_sources_sha256']),idf_commit=manifest['idf_commit'],sdkconfig_sha256=manifest['comparable_sdkconfig_sha256']))
(E/'source-validation.json').write_text(json.dumps(source_checks,indent=2))
shutil.copy2(S/'lvgl/demos/benchmark/lv_demo_benchmark.c',E/'lv_demo_benchmark.c')
for n in ['restored.json','manifest.json','restore.log','restore-verify.log']:
 dst=E/'restore';dst.mkdir(exist_ok=True);shutil.copy2(F/'baseline/s31-rgb/backup'/n,dst/n)
for n in ['summarize_s31_targeted.py','parse_s31_stable.py','loop_s31_proof.py','s31_pc_stable_v2.c','build_s31_stable.py','finalbuild_s31_stable.py','build_s31_pad_sample.py','run_s31_sample_v2.py','fix_s31_pad.py','build_s31_align.py','finalbuild_s31_align.py','run_s31_align.py','setup_s31_font_iram.py','build_s31_font_iram.py','ready_s31_font_iram.py','run_s31_font_iram.py','report_s31_root_cause.py','proof_s31_font_iram.py','bounds_s31_pc.py','check_s31_root.mjs']:
 dst=E/'scripts';dst.mkdir(exist_ok=True);shutil.copy2('/tmp/'+n,dst/n)
for obsolete in ['setup_s31_stable.py','setup_s31_align.py']:(E/'scripts'/obsolete).unlink(missing_ok=True)
# Failed instrumentation never contributes values to the passed tables.
(E/'excluded-attempts.json').write_text(json.dumps(dict(wrapper_not_flashed='/tmp/present-s31-hot-20261109/cancelled.json',first_pc_attempt='Dump starved idle task and triggered watchdog; excluded, original firmware restored',first_constructor_attempt='Unreferenced archive member discarded; no PC_DONE, excluded, original firmware restored',alignment64='Link failure on clean IDF; never flashed',data_tables='Only explicit tags with SUITE_PASS, two passes, no runtime error, valid counters and buffer checks'),indent=2))
PC=json.loads((E/'pc-results.json').read_text());pc={x['tag']:x for x in PC};loop=json.loads((E/'layout-and-pc/loop-layout-proof.json').read_text())
labels={'original-before':'present 原排列 · 前测','pad24':'present 仅后移24字节','original-after':'present 原排列 · 后测','aligned-before':'32B对齐 present · 前测','aligned-adapter':'32B对齐 adapter','aligned-pad24':'32B对齐 present＋24字节','aligned-after':'32B对齐 present · 后测','font-before':'位图函数IRAM present · 前测','font-adapter':'位图函数IRAM adapter','font-pad24':'位图函数IRAM present＋24字节','font-after':'位图函数IRAM present · 后测'}
def table(tags,ident):
 return '<table id="'+ident+'"><thead><tr><th>组合</th><th>非空渲染/s</th><th>line_fills/帧</th><th>I停顿 M/帧</th><th>swdraw CPU ms/帧</th></tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+v+'</td>' for v in [labels[t],f"{rows[t]['render_per_s']:.3f}",f"{rows[t]['instruction_fills_per_render']:.0f}",f"{rows[t]['instruction_stalls_per_render']/1e6:.3f}",f"{rows[t]['software_cpu_ms_per_render']:.2f}"])+'</tr>' for t in tags)+'</tbody></table>'
functions=['lv_font_get_bitmap_fmt_txt','lv_text_get_next_line.part.0','lv_font_get_glyph_dsc_fmt_txt','lv_draw_sw_blend_color_to_rgb565'];pctags=['sample-present','sample-adapter','sample-early','sample-pad24']
hot='<table id="hot"><thead><tr><th>函数</th><th>present 原排列</th><th>adapter</th><th>present 提前提取混合函数</th><th>present＋24字节</th></tr></thead><tbody>'
for fn in functions:
 hot+='<tr><td><code>'+fn+'</code></td>'+''.join('<td>'+f"{next(v['ms_per_render'] for v in pc[t]['top_functions'] if v['function']==fn):.2f}"+'</td>' for t in pctags)+'</tr>'
hot+='</tbody></table>'
looptable='<table id="loop"><thead><tr><th>组合</th><th>4bpp循环入口</th><th>行内偏移</th><th>64B缓存行数</th></tr></thead><tbody>'+''.join('<tr><td>'+x['implementation']+'</td><td>'+x['loop_start']+'</td><td>'+str(x['offset_in_cache_line'])+'</td><td>'+str(x['cache_lines'])+'</td></tr>' for x in loop)+'</tbody></table>'
fontbefore=rows['font-before']['render_per_s'];fontpad=rows['font-pad24']['render_per_s'];variation=(fontpad/fontbefore-1)*100
text='''<!doctype html><meta charset="utf-8"><title>S31 present 取指停顿根因定位</title><style>body{font:16px/1.75 system-ui;max-width:1250px;margin:36px auto;padding:0 18px;color:#24344a}table{border-collapse:collapse;width:100%;margin:18px 0}td,th{border:1px solid #cbd5e1;padding:8px;text-align:right}td:first-child,th:first-child{text-align:left}th{background:#edf3fa}code{overflow-wrap:anywhere}aside{padding:14px 18px;background:#edf5fe;border-left:4px solid #3572be}a{color:#215a9d}.muted{color:#53657c}</style><h1>S31：present 取指停顿偏多，定位到哪里了</h1><p><a href="comparison.html">24项完整16场景缓存对照</a> · <a href="layout.html">第一轮链接顺序对照</a></p><aside><b>已经证实：本轮差距受最终指令缓存布局显著影响，主要异常热点落在 LVGL 的4bpp字形位图转A8路径；不能直接归因为 present 像素处理量更多或 pixel worker 派发。</b><br>生产源码与IDF均未修改；下面全是独立诊断，原正式帧率／内存矩阵不替换。</aside><h2>1. 相同字体数据，只后移24字节</h2><p>XIP关闭，800×480，DOUBLE_PARTIAL，单输入tile80000B，双方RGB bounce均10行。每组两次18秒全屏文字，预热约1秒；前后基线夹住干预。字体位图、glyph_dsc、cmaps、font_dsc共12个对象，以及font20描述符，地址保持不变；只增加24字节不执行的代码填充。</p>'''+table(['original-before','pad24','original-after'],'plain')+'''<p>指令访问总量接近，但缓存行补入和停顿明显减少，吞吐随之恢复；恢复原排列后慢基线再次出现。两边bounce配置一致；pixel worker绕过对照也未追回全屏文字吞吐。</p><h2>2. 时间主要差在哪个函数</h2><p>每核1kHz中断PC采样，64字节分箱。以下是近似CPU驻留ms/渲染帧，多核相加；<b>不是每函数miss计数，也不是单次调用耗时</b>。分箱边界会使函数归属有误差。采样器全部位于IRAM，直读该镜像ELF验证的measuring标志地址，保留原probe，不插入热路径调用；逐镜像确认LVGL全局函数地址未改变。font＋24字节采样与该对照的字体数据／热函数地址一致。</p>'''+hot+'''<p>考虑64B分箱边界，原排列位图函数的范围为29.80–36.09ms/帧，adapter为11.72–18.14ms/帧，区间仍不重合；<a href="evidence/root-cause/pc-function-bounds.json">边界归属上下界</a>已归档。位图解包从约29.8ms降到约12ms，其他几条主要字体／混合路径变化较小。此场景使用Montserrat20、4bpp、非压缩字体；热点是每次把字形覆盖率展开成A8，再供LVGL混合，并非视频解码或RGB扫描。</p><h2>3. 不能把跨缓存行直接等同于miss</h2><p>同一段48字节4bpp内循环，四份最终ELF的循环机器码SHA256完全一致：</p>'''+looptable+'''<p>adapter和提前提取版本也跨两行，却更快。因此“跨两行就慢”不成立。已经有反事实实验证明的是<b>整体链接排列改变了缓存表现</b>；具体哪几组cache set／替换序列互相干扰，尚未通过硬件trace直接观测，不能据此宣称IDF缓存硬件bug。</p><h2>4. 全局32字节对齐不是修复</h2><p>对双方LVGL统一使用-fmin-function-alignment=32；在ELF中验证热点入口对齐。普通-falign-functions=64在-Os下被忽略，64字节最小函数对齐又触发当前干净IDF的链接区间冲突，未上板、未修改IDF。</p>'''+table(['aligned-before','aligned-adapter','aligned-pad24','aligned-after'],'aligned')+'''<p>增加24字节后仍明显退化，否决将全局32字节对齐当作稳定修复。</p><h2>5. 只把位图解包函数放进IRAM</h2><p>应用级链接片段，对present和adapter完全相同：<code>lv_font_fmt_txt:lv_font_get_bitmap_fmt_txt (noflash)</code>，archive为liblvgl.a。函数最终1006字节，present的IRAM段增加约1012字节；不新增像素、tile、compose或传输缓冲，不改变扫描同步。它是代码放置优化，<b>不表示整个字体绘制链路可在cache关闭时运行</b>。</p>'''+table(['font-before','font-adapter','font-pad24','font-after'],'font')+f'''<p>在这组验证中，增加24字节后的吞吐变化为{variation:+.2f}%。此结论只覆盖本次S31、XIP关闭、全屏文字诊断，不能替代其他场景和布局的完整回归。精确链接片段、源码、SDK配置与日志都已归档；尚未把它默认接入生产组件。</p><h2>6. 对后续设计的含义</h2><p>不能用某一份固件的总分直接推断present的层次／异步架构更慢：这里相同LVGL目标代码，只改变最终放置就出现显著波动。S31本轮每核16KiB指令缓存、64B行；Flash miss代价高，XIP已验证会显著降低每次补入的停顿代价，但不会使双方补入次数自动趋同。</p><p>应把扫描/像素后端成本和LVGL字体热点分开评估，回归中保留小幅链接扰动对照；针对已定位热函数使用小范围IRAM映射，比盲目全局对齐更可控。是否推广该映射，仍需以完整场景回归和IRAM预算决定。</p><p class="muted">计数覆盖整机I-bus；请求冲突为requester arbitration计数，不是缓存组冲突，也不能代表所有外存总线争用都为零。miss口径参考<a href="https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html">IDF Cache Access Counters</a>。</p><p><a href="evidence/root-cause/targeted-results.json">30项定点计数</a> · <a href="evidence/root-cause/pc-results.json">PC分箱原始数据</a> · <a href="evidence/root-cause/layout-and-pc/loop-layout-proof.json">循环机器码与位置证明</a> · <a href="evidence/root-cause/font-iram/ready-builds.json">IRAM落点证明</a> · <a href="evidence/root-cause/restore/restored.json">最终整片恢复校验</a></p>'''
(O/'root-cause.html').write_text(text)
p=O/'comparison.html';s=p.read_text();note='<p><a href="root-cause.html">进一步根因定位：字体热点、24字节扰动和小范围IRAM验证</a></p>'
if 'href="root-cause.html"' not in s:s=s.replace('</body>',note+'</body>') if '</body>' in s else s+note
p.write_text(s)
p=O/'README.md';s=p.read_text()
if 'root-cause.html' not in s:s+='\n[继续定位的完整证据](root-cause.html)：额外30项18秒定点运行，24字节平移、PC热点、全局32B对齐反证及单函数字体解包IRAM验证；没有覆盖正式分数。最终原固件整片恢复校验通过。\n'
p.write_text(s)
(E/'validation.json').write_text(json.dumps(dict(passed=True,targeted_cases=30,pc_images=4,samples_first_case_only=True,counter_input_capacity_verified=True,original_firmware_restored=True,production_and_idf_unchanged=True),indent=2))
(E/'README.md').write_text('最终复现输入位于 images/*/test-inputs；sdkconfig、manifest、额外 .S/.lf 均逐镜像归档。PC采样仅为诊断：pc_address.h必须从该次最终ELF重新提取并验证，不能跨固件使用。源模板迭代中失败的采样与对齐尝试不参与通过表；这里只保留最终源码、采集控制器、数据处理和核验脚本。\n')
print('Root-cause report generated',O/'root-cause.html')
