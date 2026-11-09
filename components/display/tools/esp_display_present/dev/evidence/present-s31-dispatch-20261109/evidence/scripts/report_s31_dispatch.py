from pathlib import Path
import json,sys,html,gzip,shutil,subprocess,hashlib,importlib.util,statistics,difflib
R=Path('/tmp/present-s31-dispatch-20261109')
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');O=C/'deliveries/present-s31-dispatch-20261109'
preview='--preview' in sys.argv
if preview:O=R/'preview'
O.mkdir(exist_ok=True,parents=True)
subprocess.run([sys.executable,'/tmp/summarize_s31_dispatch.py',str(O)],check=True,stdout=subprocess.DEVNULL)
raw=json.loads((O/'raw-results.json').read_text());summary=json.loads((O/'summary.json').read_text());lookup={(r['xip'],r['execution'],r['rotation'],r['scene']):r for r in summary};passed=sum(c['passed'] for x in raw for c in x['cases'])
if not preview:
 status=json.loads((R/'completion.json').read_text());assert status['success'] and status['restored'];assert passed==24 and sum(len(c['scenes']) for x in raw for c in x['cases'])==384
 assert len(summary)==192 and all(r['repeats']==2 for r in summary)
for name in ['scope.json','dispatch-only.patch','ready-builds.json','backup-proof.json','xip-config-diff.json','lvgl-code-proof.json','code-placement.json']:
 if (R/name).exists():shutil.copy2(R/name,O/name)
labels={'worker':'present worker','inline':'present 调用线程执行','adapter':'adapter'}
focus=['Screen sized text','Multiple labels','Multiple arcs','Moving wallpaper']
def rows_for(scene):
 return sorted((r for r in summary if r['scene']==scene),key=lambda r:(["off","on"].index(r['xip']),r['rotation'],['worker','inline','adapter'].index(r['execution'])))
def table(scene):
 h='<div class="scroll"><table class="cache"><thead><tr><th>XIP</th><th>角度</th><th>执行路径</th><th>次数</th><th>有效绘制/s（范围）</th><th>完成访问 M/帧</th><th>真实填充/帧</th><th>填充/访问</th><th>停顿事件 M/帧</th><th>停顿/填充</th><th>请求冲突/帧</th></tr></thead><tbody>'
 for r in rows_for(scene):
  h+=f'<tr><td>{r["xip"]}</td><td>{r["rotation"]}°</td><td>{labels[r["execution"]]}</td><td>{r["repeats"]}</td><td>{r["render_per_s"]:.2f} ({r["render_per_s_min"]:.2f}–{r["render_per_s_max"]:.2f})</td><td>{r["instruction_accesses_per_render"]/1e6:.2f}</td><td>{r["instruction_fills_per_render"]:.0f}</td><td>{r["instruction_fill_access_ratio"]*100:.4f}%</td><td>{r["instruction_stalls_per_render"]/1e6:.2f}</td><td>{r["instruction_stall_fill_ratio"]:.2f}</td><td>{r["instruction_conflicts_per_render"]:.2f}</td></tr>'
 return h+'</tbody></table></div>'
# Exact decomposition: stalls/frame = fills/frame * stalls/fill; all counters use pooled raw sums.
decompositions=[]
for v in ['off','on']:
 for a in [0,90]:
  for other in ['adapter','inline']:
   w=lookup.get((v,'worker',a,'Screen sized text'));b=lookup.get((v,other,a,'Screen sized text'))
   if not w or not b:continue
   d=dict(xip=v,rotation=a,other=other,stall_factor=w['instruction_stalls_per_render']/b['instruction_stalls_per_render'],fill_factor=w['instruction_fills_per_render']/b['instruction_fills_per_render'],stall_per_fill_factor=w['instruction_stall_fill_ratio']/b['instruction_stall_fill_ratio'],render_factor=b['render_per_s']/w['render_per_s'])
   assert abs(d['stall_factor']-d['fill_factor']*d['stall_per_fill_factor'])<1e-10
   decompositions.append(d)
(O/'decomposition.json').write_text(json.dumps(decompositions,indent=2)+'\n')
intro='''本轮只定位S31 Korvo 800×480的DOUBLE_PARTIAL，0°/90°，单输入tile，双方分别80000B/48000B。两边均为10行RGB bounce，每块16000B、共两块，板级代码和IDF相同。XIP开／关各跑：present每角度worker→调用线程→调用线程→worker；adapter每角度重复两次。共24项、384个完整场景。每组两次；小幅差异不作为统计显著结论。CPU配置320MHz、Flash 80MHz DIO、PSRAM 200MHz，I-cache 16KiB、D-cache 64KiB，双方一致。'''
method='''同一份present固件在用例边界切换执行方式，保留pixel worker、后台sender及其资源；只在隔离副本的pixel_submit选择现有同步执行分支。ticket、完成通知、回收与scanout退役契约保持。这里的调用线程可能是UI或后台提交者，不把它误写为所有复制都在UI执行。仓库生产源码、IDF未修改。adapter保持logical-50隔离分配overlay。'''
semantics='''计数是两个I-bus的整机窗口总和，不是仅LVGL函数；按非空render次数归一化。每场景预热至少1秒，每250ms采集并清零，饱和项拒收，关闭显示前停止计数，关闭后再打印。line_fills是真实填充次数；stall_events在缺失等待期间重复累加，只作相对指标，不换算为CPU周期或毫秒。request conflicts表示请求者仲裁冲突，不是cache set映射冲突；零值不能排除缓存行被挤出。'''
findings=['两边139个LVGL源码对象的重定位前.text机器码一致，最终链接地址不同；这排除了本轮LVGL源码对象编译差异，尚不能证明具体缓存映射冲突。']
for d in decompositions:
 if d['xip']=='off' and d['other']=='adapter':findings.append(f'XIP off、{d["rotation"]}°全屏文字：worker/adapter停顿比{d["stall_factor"]:.2f} = 填充次数比{d["fill_factor"]:.2f} × 每次填充停顿比{d["stall_per_fill_factor"]:.2f}。')
for a in [0,90]:
 w=lookup.get(('off','worker',a,'Screen sized text'));i=lookup.get(('off','inline',a,'Screen sized text'))
 if w and i:findings.append(f'XIP off、{a}°绕过pixel派发：有效绘制{w["render_per_s"]:.2f}→{i["render_per_s"]:.2f}/s，停顿{w["instruction_stalls_per_render"]/1e6:.2f}→{i["instruction_stalls_per_render"]/1e6:.2f}M/帧。')
if all(k in lookup for k in [('off','worker',0,'Screen sized text'),('off','inline',0,'Screen sized text'),('off','adapter',0,'Screen sized text')]):
 w=lookup['off','worker',0,'Screen sized text'];i=lookup['off','inline',0,'Screen sized text'];a=lookup['off','adapter',0,'Screen sized text']
 findings.append(f'0°直接执行仍有{ i["instruction_fills_per_render"]/a["instruction_fills_per_render"]:.2f}倍adapter的填充；本轮未通过绕过pixel worker追回文字吞吐。这个对照不支持把像素worker派发当成该场景的主要根因。')
 findings.append('填充次数偏高已经测到；具体是哪些热函数／代码布局／执行交错造成，尚未独立定位。应继续定位热代码驻留，不能仅凭本轮将生产路径整体改成同步。')
w=lookup.get(('on','worker',0,'Screen sized text'));a=lookup.get(('on','adapter',0,'Screen sized text'))
if w and a:
 findings.append(f'XIP on、0°：worker/adapter填充比仍为{w["instruction_fills_per_render"]/a["instruction_fills_per_render"]:.2f}，但停顿/填充为{w["instruction_stall_fill_ratio"]:.2f}/{a["instruction_stall_fill_ratio"]:.2f}。XIP改善了等待代价，没有让本组填充次数收敛。')
page='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S31 取指停顿与单tile派发对照</title><style>body{font:15px/1.7 system-ui,sans-serif;color:#172b3f;background:#f3f6fa;margin:0;padding:24px}main{max-width:1500px;margin:auto}section{background:white;padding:18px;margin:18px 0;border-radius:12px}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;white-space:nowrap}th,td{padding:9px 12px;border-bottom:1px solid #dce4ed;text-align:right}th{background:#eaf0f7}td:nth-child(3),th:nth-child(3){text-align:left}h1,h2{line-height:1.4}a{color:#1764ac}.muted{color:#53677a}code{background:#eaf0f7;padding:2px 5px}</style><main>'''
page+='<h1>'+('进行中预览 · ' if preview else '')+'S31：为什么 present 取指停顿更多</h1><section><p>'+intro+'</p><p>'+method+'</p><p>'+semantics+'</p><p><a href="scenes.csv">全部逐场景CSV</a> · <a href="summary.json">分组数据</a> · <a href="README.md">口径与证据</a></p></section><section><h2>本轮实测</h2>'+''.join('<p>'+html.escape(f)+'</p>' for f in findings)+'<p class="muted">只能据此分解填充频率与等待贡献；具体热函数／缓存映射冲突仍需独立证据。两边是不同固件，链接布局不同；worker/调用线程之间才是同一固件的受控对照。探针会改变链接布局与调度，本轮只在同版内对照，诊断版不替换原正式矩阵成绩。</p></section>'
for scene in focus:
 page+='<section><h2>'+scene+'</h2>'+table(scene)+'<details><summary>绘制与执行成本</summary><div class="scroll"><table class="cost"><thead><tr><th>XIP</th><th>角度</th><th>路径</th><th>像素/帧</th><th>flush/帧</th><th>SW CPU ms/帧</th><th>UI＋service CPU ms/帧</th><th>复制执行墙钟 ms/帧</th></tr></thead><tbody>'
 for r in rows_for(scene):page+=f'<tr><td>{r["xip"]}</td><td>{r["rotation"]}°</td><td>{labels[r["execution"]]}</td><td>{r["pixels_per_render"]:.0f}</td><td>{r["flushes_per_render"]:.2f}</td><td>{r["software_cpu_ms_per_render"]:.2f}</td><td>{r["ui_workers_cpu_ms_per_render"]:.2f}</td><td>{format(r["execute_wall_ms_per_render"],".2f") if r["executor_instrumented"] else "—"}</td></tr>'
 page+='</tbody></table></div><p>CPU为任务累计运行时间；两核并行总和可超过帧墙钟。复制执行含等待，不能与其他嵌套指标相加。adapter执行函数没有对应插桩，其复制执行列为“—”，不表示没有复制成本。</p></details></section>'
page+='<footer><a href="https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html">Espressif计数语义</a><p>原固件恢复状态：'+('进行中' if preview else '已恢复并独立整片校验')+'。串口完成不代替目视时序验收。</p></footer></main></html>'
(O/'comparison.html').write_text(page)
notes=['# S31 取指停顿与单tile派发对照','',intro,'',method,'',semantics,'','## 结果','']+findings+['','探针会改变链接布局与调度，本轮只在同版内对照，不把新旧停顿数差异当作严格因果。具体热函数／cache set冲突尚不能由这些计数独立定位；不把request-conflicts当作set-conflicts。两实现不同链接布局会影响缓存行为。worker/inline两组位于同一固件，只改变像素请求是否派发给worker，后台sender仍保留，不能将结果外推为所有任务交接都无影响。','','公开数据的完整16场景成绩与诊断结果分开，本轮不覆盖原矩阵。原始CPU100%无效，CPU成本使用FreeRTOS任务运行时间。文字场景每帧固定384000像素，0°10次flush、90°16次flush，优先用于分解；其他动画场景应结合像素/帧判断，不能把文字结论外推全部PARTIAL。两次重复只给出范围，不宣称小幅变化具有显著性。','']
(O/'README.md').write_text('\n'.join(notes))
if preview:print('Preview',passed,'cases');raise SystemExit(0)
# Runtime identity: only the explicit dispatch patch may differ from the committed production components.
repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR');scope=json.loads((R/'scope.json').read_text());proof=[]
for path in ['esp_display_present','esp_lv_present','esp_lvgl_adapter']:
 for sub in ['src','include']:
  base=R/'off/source/components/display/tools'/path/sub
  for p in sorted(base.rglob('*')):
   if not p.is_file():continue
   rel='components/display/tools/'+str(p.relative_to(R/'off/source/components/display/tools'))
   old=subprocess.check_output(['git','-C',str(repo),'show',scope['revision']+':'+rel]);same=old==p.read_bytes();assert same or rel.endswith('/src/pixel/present_pixel_engine.c'),rel
   proof.append(dict(path=rel,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),same_as_committed=same))
assert sum(not x['same_as_committed'] for x in proof)==1
(O/'source-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
idf=Path('/home/xuxin/esp_work/esp-idf-qspi');assert not subprocess.check_output(['git','-C',str(idf),'diff','HEAD','--name-only'],text=True).strip()
for v in ['off','on']:
 for impl in ['present','adapter']:
  src=R/v/f's31-rgb/benchmark/{impl}';dst=O/'evidence'/v/impl;dst.mkdir(parents=True,exist_ok=True)
  for n in ['manifest.json','sdkconfig','bench_cases.h','diagnostic-results.json']:shutil.copy2(src/n,dst/n)
  for n in ['live.log','build.log']:(dst/(n+'.gz')).write_bytes(gzip.compress((src/n).read_bytes(),mtime=0))
original=R/'off/source/components/display/tools/esp_lvgl_adapter/src/display/display_manager.c'
for v in ['off','on']:
 p=R/v/'s31-rgb/benchmark/adapter/esp_lvgl_adapter/src/display/display_manager.c'
 m=json.loads((R/v/'s31-rgb/benchmark/adapter/manifest.json').read_text());assert hashlib.sha256(p.read_bytes()).hexdigest()==m['adapter_allocation_shim_sha256']
(O/'adapter-allocation-only.patch').write_text(''.join(difflib.unified_diff(original.read_text().splitlines(True),p.read_text().splitlines(True),fromfile='a/esp_lvgl_adapter/src/display/display_manager.c',tofile='b/esp_lvgl_adapter/src/display/display_manager.c')))
# Exact test sources plus runtime patch; no build or firmware binaries.
source=R/'off/source/components/display/tools/esp_display_present/dev';manifest=json.loads((R/'off/s31-rgb/benchmark/present/manifest.json').read_text())
for name,sha in manifest['test_sources_sha256'].items():
 p=source/name;assert hashlib.sha256(p.read_bytes()).hexdigest()==sha;dst=O/'evidence/test-inputs'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
for sub in ['test_apps/benchmark/CMakeLists.txt','boards/configs/base.defaults','boards/configs/lvgl.defaults','boards/configs/s31-rgb.defaults']:
 p=source/sub;dst=O/'evidence/test-inputs'/sub;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
p=R/'lvgl/demos/benchmark/lv_demo_benchmark.c';dst=O/'evidence/test-inputs/lv_demo_benchmark.c';shutil.copy2(p,dst)
shutil.copy2(R/'lvgl/LICENCE.txt',O/'evidence/test-inputs/LVGL-LICENCE.txt')
restore=O/'evidence/restore';restore.mkdir(parents=True,exist_ok=True)
for n in ['manifest.json','restored.json']:shutil.copy2(R/'off/s31-rgb/backup'/n,restore/n)
for n in ['restore.log','restore-verify.log']:(restore/(n+'.gz')).write_bytes(gzip.compress((R/'off/s31-rgb/backup'/n).read_bytes(),mtime=0))
for n in ['identify.log','backup-current-verify.log','controller.log','build-controller.log','rebuild-controller.log','finalbuild-controller.log','initial-off-present-build.log','initial-off-present-controller.log']:
 if (R/n).exists():(O/'evidence'/(n+'.gz')).write_bytes(gzip.compress((R/n).read_bytes(),mtime=0))
shutil.copy2(R/'events.jsonl',O/'events.jsonl');shutil.copy2(R/'completion.json',O/'completion.json')
scripts=O/'evidence/scripts';scripts.mkdir(parents=True,exist_ok=True)
for n in ['setup_s31_dispatch.py','patch_s31_dispatch_exec.py','backup_s31_dispatch.py','build_s31_dispatch.py','run_s31_dispatch.py','parse_s31_dispatch.py','summarize_s31_dispatch.py','report_s31_dispatch.py','proof_s31_dispatch_elf.py','check_s31_dispatch.mjs','validate_s31_dispatch.py']:
 shutil.copy2('/tmp/'+n,scripts/n)
(O/'validation.json').write_text(json.dumps(dict(passed=True,cases=24,scene_records=384,groups=192,repetitions=2,restored=True,idf_patched=False,production_checkout_modified=False,diagnostic_dispatch_patch=True),indent=2)+'\n')
print('Final report',O)
