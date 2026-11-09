from pathlib import Path
import sys,json,re,gzip,csv,html,shutil,hashlib,difflib,subprocess
sys.path.insert(0,'/tmp');from parse_s31_deep import parse
R=Path('/tmp/present-s31-deep-analysis-20261109');C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');O=C/'deliveries/present-s31-deep-analysis-20261109';O.mkdir(parents=True,exist_ok=True)
state=json.loads((R/'stage.json').read_text());assert state['action']=='done' and state['success'] and state['restored'],state
cases=[]
for v in ['off','on']:
 for impl in ['present','adapter']:
  src=R/v/f's31-rgb/benchmark/{impl}';parsed=parse(src/'live.log',v,impl);assert len(parsed)==2 and all(c['passed'] and len(c['scenes'])==16 for c in parsed)
  assert all(s['cache']['saturated']==0 and s['errors']==0 and s['us']>0 for c in parsed for s in c['scenes'].values())
  cases+=parsed;d=O/f'evidence/{v}/{impl}';d.mkdir(parents=True,exist_ok=True)
  for n in ['results.json','manifest.json','sdkconfig','bench_cases.h']:shutil.copy2(src/n,d/n)
  for n in ['live.log','build.log']:(d/(n+'.gz')).write_bytes(gzip.compress((src/n).read_bytes(),mtime=0))
lookup={(c['variant'],c['implementation'],c['rotation']):c for c in cases}
# Audit common SDK symbols; implementation-specific Kconfig namespaces differ.
def sdk(p):
 d={}
 for l in p.read_text().splitlines():
  if l.startswith('CONFIG_') and '=' in l:k,v=l.split('=',1);d[k]=v
  elif l.startswith('# CONFIG_') and l.endswith(' is not set'):d[l[2:-11]]='n'
 return d
sdk_audit={}
for v in ['off','on']:
 p=sdk(R/v/'s31-rgb/benchmark/present/sdkconfig');a=sdk(R/v/'s31-rgb/benchmark/adapter/sdkconfig')
 diff={k:[p[k],a[k]] for k in p.keys()&a.keys() if p[k]!=a[k]};assert not diff,diff
 sdk_audit[v]={'shared_symbols_identical':True,'shared_symbol_count':len(p.keys()&a.keys()),'common_differences':diff,'present_only':{k:p[k] for k in sorted(p.keys()-a.keys())},'adapter_only':{k:a[k] for k in sorted(a.keys()-p.keys())}}
(O/'sdkconfig-pair-audit.json').write_text(json.dumps(sdk_audit,indent=2)+'\n')
flat=[]
for c in cases:
 for s in c['scenes'].values():
  cache=s['cache'];n=max(1,s['render']);s['i_stall_m_per_render']=(cache['im0']+cache['im1'])/n/1e6;s['i_access_m_per_render']=(cache['ih0']+cache['ih1'])/n/1e6;s['d_stall_m_per_render']=(cache['dm0']+cache['dm1'])/n/1e6
  flat.append({**{k:c[k] for k in ['variant','implementation','mode','rotation']},**{k:v for k,v in s.items() if k not in ['cache','tasks','lvgl']}})
(O/'results.json').write_text(json.dumps(cases,indent=2,ensure_ascii=False)+'\n')
with (O/'scenes.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(flat[0]),lineterminator="\n");w.writeheader();w.writerows(flat)
# Common capacities: one input tile; fallback allocation is listed separately.
assert all(s['capacity']==(80000 if c['rotation']==0 else 48000) for c in cases for s in c['scenes'].values() if s['flush'])
old=json.loads((C/'deliveries/present-adapter-five-board-20261109/comparison-current.json').read_text())
mem=[]
for row in old:
 if row['board']=='s31-rgb' and row['rotation'] in [0,90] and row['mode']!='AUTO':
  m={k:row[k] for k in ['mode','rotation']}
  for impl in ['present','adapter']:
   for heap in ['internal','psram']:m[f'{impl}_{heap}_open_KiB']=(row[f'{impl}_{heap}_before_open_free_bytes']-row[f'{impl}_{heap}_after_open_free_bytes'])/1024
  mem.append(m)
(O/'original-memory-analysis.json').write_text(json.dumps(mem,indent=2)+'\n')
config=json.loads((R/'configuration-audit.json').read_text())
# Summaries are duration-weighted steady-state windows, never the LVGL arithmetic grade.
def summary(c):
 ss=list(c['scenes'].values());us=sum(s['us'] for s in ss);n=sum(s['render'] for s in ss);refr=sum(s['refresh'] for s in ss)
 return dict(grade=c['grade']['lvgl_fps'],seconds=us/1e6,render_fps=n*1e6/us,refresh_fps=refr*1e6/us,empty_pct=sum(s['empty'] for s in ss)*100/max(1,refr),panel_fps=sum(s['panel'] for s in ss)*1e6/us,stall_m_frame=sum(s['cache']['im0']+s['cache']['im1'] for s in ss)/max(1,n)/1e6,sw_ms_frame=sum(sum(t['us'] for t in s['tasks'] if t['name']=='swdraw') for s in ss)/max(1,n)/1000)
summaries=[dict(variant=c['variant'],implementation=c['implementation'],rotation=c['rotation'],**summary(c)) for c in cases]
(O/'summaries.json').write_text(json.dumps(summaries,indent=2)+'\n')
text='''# S31 RGB 全面定位：把访问路径、交接成本和统计口径分开

生产源码未修改。只对 S31 Korvo 800×480 做新硬件诊断，其他四板做现有记录及配置审计；不能将本专项当作全模式重新验收。

## 已确认的问题

1. **此前“双方开 XIP 后完全收敛”过度概括。** 相等的是 16 场景 FPS 的整数算术均值，场景差异可以相互抵消。综合成绩不能代表每个场景或实际送屏帧率相同。
2. **同样 50 行不代表同容量。** 原 adapter 按物理宽 800 分配 80000B；present 的 90° 按逻辑宽 480 分配 48000B。原对比混入了分块大小、flush 次数和运行内存差异。本轮统一 0° 80000B、90° 48000B，双方始终只有一个输入 tile；LVGL 软件绘制线程仍同为 2 个。
3. **LVGL FPS 包含空刷新。** sysmon 在 REFR_READY 增加 refr_cnt，未要求出现 RENDER_START。15ms 刷新周期的统计上限为 66；这不是面板每秒扫描的新帧数。本轮分别统计刷新、非空绘制和成功 panel draw_bitmap 提交，后者也不等于实际扫描完成。
4. **原 CPU 100% 不可信。** 两边 ELF 都保留 lv_os_get_idle_percent，却没有 lv_freertos_task_switch_in/out；默认 idle 计数没有接入任务切换。默认 idle 返回 0 导致 CPU 显示 100%。本轮改用 FreeRTOS 任务累计运行时间审计，未修改原始成绩。
5. **借用缓冲后仍分配保底像素存储。** present_lvgl.c:create_binding 无条件 lv_draw_buf_create，pooled PARTIAL 用它作保底，FULL/DIRECT 也分配整屏。S31 整屏 RGB565=768000B=750KiB。原 0° FULL/DIRECT 打开增量约 753KiB，对照 adapter 约 3–6KiB；90° present 约 1503KiB，还包含独立逻辑画布。此处有明确的内存优化空间，不能用 XIP 静态占用解释。
6. **单 tile 的异步管线有交接成本。** present 的 begin/acquire/submit/commit、像素 worker、后台 sender 和完成通知维护明确的所有权；adapter 的 PARTIAL 大多在 UI flush 内同步调用复制/旋转。一个 tile 在复制完成前不能被下一段绘制复用，因此异步接口本身不保证多段绘制与复制重叠。任务统计将两个 present_service 合计，不把同名任务擅自归因到单个角色。adapter 没有这两个专用 worker，其复制成本计入 UI 任务，不能把缺少 worker 误读成零成本。

## XIP 为什么影响这么大

原 S31 使用 80MHz DIO Flash、200MHz Octal PSRAM、16KiB I-cache、64KiB D-cache；S3 原测试则已经开启 XIP 且 I-cache 为 32KiB，两板不是相同的取指条件。S3 没有 S31 的 DMA2D/PPA；本 S31 日志确认 0° 走 DMA2D、90° 走 PPA，S3 的对应复制/旋转主要走 CPU。所以同样接 RGB，不代表像素处理路径和每块任务的启动成本相同。

XIP 将代码和只读数据从 Flash 移入 PSRAM，改变缓存未命中后的服务路径，并未消除 present 的状态机、锁、复制或任务切换。两个实现的 LVGL 可重定位 .text 字节一致，但最终地址、热路径组合和调度交错不同；adapter 的 rotate_copy_strided_region 在 IRAM，present 的部分交接函数仍在外部可执行区。LVGL 的 RGB565 图像混合函数单个就有 9728B。不能仅凭这些静态事实断定具体哪一组函数发生 I-cache 冲突。

本轮采集硬件“miss”原始寄存器时，按官方语义解释为**缺失访问期间的停顿事件**，不是缺失访问次数，更不能用 miss/(hit+miss) 算 miss 率。用每个非空绘制帧的停顿量作相对对照；I/D 两条路径分别列出。尚未采集 line_fills，不能进一步分解“未命中更多”和“每次缺失更慢”，也未将代码取指与字体/图片 rodata 的 XIP 贡献单独隔离。

原不带计时四项（双0/90、三0/90）为 present 17/17/24/23、adapter 19/24/27/29；同开 XIP 为双方 37/36/40/40。这证明该访问路径对这组成绩影响大，不证明所有模式、场景都等价。之前 burst32→128 和重复 cache 维护精简的无计时复核仍为 present 17/17/24/23，未追回差距，不再把它们当作主因。

## 已核对但不应误判为主因的项

- **不是整条显示链都更慢。** 同开XIP、同容量后，移动壁纸0°双方约42.1次有效绘制/秒，90°双方约21.08次/秒。该全屏场景已经基本一致；多标签的剩余差距应单独看小块请求路径。
- **不是“单tile”就意味着只有一份像素分配。** 本轮单tile限定的是输入池的活动tile数量；binding保底存储、旋转逻辑画布和panel framebuffer属于不同角色，必须分别记账。
- **不把stride自动标志当作PARTIAL根因。** 两边设置LVGL draw buffer的方式不同，但本版本PARTIAL绘制仍按实际子区域重塑stride；不能仅凭stride_is_auto标志不同断言固定整屏stride错误。
- **不通过放宽退役时序提速。** 本轮没有修改RGB bounce/VSYNC通知和framebuffer退役契约。串口性能记录也没有重新证明目视防撕裂；本次结论不支持提前释放扫描缓冲。
- **burst/cache维护不是这轮主要增益。** 此前单项及组合实验没有追回差距；在没有新证据前不继续围绕同一微调重复试验。

## 应优先处理什么

- 先修测试口径：同时展示非空绘制/送屏提交，按实际字节统一输入 tile 容量，真实 CPU 统计单列；保留原始 benchmark 分数便于追溯。
- 解耦 LVGL 保底描述符和整块像素存储。优先削减 FULL/DIRECT 的整屏重复分配；必须证明 detach、取消、销毁、旋转期间不会写到正在扫描的缓冲，不能直接删掉保底或指向 scanout。
- 再针对单 tile 交接路径优化。先用每帧 worker CPU、等待和停顿量找到耗时段，再考虑收敛通知/短任务的派发，以及在实测支持的阈值下让很小的复制请求使用 CPU。不能未经验证就更换所有模式的执行器。保持 framebuffer 退役契约，不以提前 flush_ready 或提前释放缓冲换成绩。
- XIP 是可选的性能/内存取舍。此前测得 present 约消耗 925.50KiB PSRAM 堆容量及 10.44KiB 内部堆容量；内部差值来自 IRAM text、DRAM data/BSS 和对齐，是静态链接布局开销，不是一个 10KiB 显示缓冲。
- FULL/DIRECT、三缓冲、180°/270°只能引用已有测试，不能把这次 DOUBLE_PARTIAL 两角度结果外推。新优化再以对应模式的硬件复测验收。

## 本轮诊断方法及边界

干净 IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe；生产源码与 HEAD 535d753dc 的 99 个 src/include 文件一致。诊断只在隔离副本加入 benchmark 场景入口、事件/任务/缓存计数和错误定位。两边都启用相同 FreeRTOS 运行时间统计，保持单输入 tile、同容量、同绘制线程数和共享 SDK 配置；组件独有 Kconfig 单列于 sdkconfig-pair-audit.json。

每个固件跑完整 16 场景，0°/90°连续运行；统计窗口略去每场景最初至少 1s。任务运行时间按两个核的累计时间归一化；软件线程的总 CPU 毫秒可以大于单帧墙钟时间。事件窗口可能截断一帧，render 与 panel 次数可能有边界差 1；panel 指标是提交成功率，不是扫描完成率。各阶段等待/绘制会嵌套，不相加推导整帧时间。窗口加权吞吐与 LVGL 16 场景等权整数分数不同。

诊断会改变代码布局、调度和开销；只在同一诊断版本内比较，不覆盖之前的正式矩阵。每组一轮，不宣称小差值具统计显著性；串口通过不替代目视防撕裂验收。

首轮诊断的 90° 在关闭时报 ESP_ERR_INVALID_STATE；保留在 evidence/attempt1，不计为通过。当时还出现缓存计数饱和。修正版本把日志移到关闭之后，并每 250ms 左右采集/清零统计寄存器；成功记录要求 saturated=0。重跑通过不能证明首轮错误根因已完全查清，不能将其写成生产缺陷已修复。

原固件已完整恢复，16MiB 独立校验通过。二进制备份保存在 hardware-backups，不随报告交付。报告不含 build 或固件。

## 原始证据与官方口径

- [本轮逐场景诊断 CSV](scenes.csv)、[完整结构化记录](results.json)、[旧五板 2896 条逐场景原始统计](original-scenes.csv)。旧记录保持既有口径，并非本轮新跑。
- [五板配置审计](configuration-audit.json)、[本轮共享项及组件独有项](sdkconfig-pair-audit.json)、[生产源码一致性](production-source-proof.json)、[代码位置](code-placement.json)、[idle hook 审计](idle-hook-audit.json)、[本轮控制流程](events.jsonl)。
- [IDF S31 缓存计数语义](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html)：accesses 为完成访问，stall_events 为缺失等待期间的重复事件，line_fills 才用于计算真实 miss ratio。当前固定 IDF 尚无该辅助 API，本轮直接读取其寄存器，未向 IDF 打补丁。
- [IDF S31 XIP 说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-guides/external-ram.html)。
'''
# Add a focused, like-work scene comparison to ground the cache explanation.
focus=['\n## 本轮屏幕文字场景（固定全屏像素工作量）\n','| XIP | 角度 | 实现 | 有效绘制/s | 软件绘制CPU ms/帧 | I-bus完成访问M/帧 | I-bus停顿M/帧 |','|---|---:|---|---:|---:|---:|---:|']
for v in ['off','on']:
 for a in [0,90]:
  for impl in ['present','adapter']:
   z=lookup[v,impl,a]['scenes'][8]
   focus.append(f"| {v} | {a} | {impl} | {z['render_per_s']:.2f} | {z['software_draw_ms_per_render']:.2f} | {z['i_access_m_per_render']:.2f} | {z['i_stall_m_per_render']:.2f} |")
focus+=['','本场景每个有效帧都是384000像素。I-bus计数为整机两条请求总线的窗口累计，包含框架和其他任务，不是仅LVGL指令数；停顿事件仅作相对指标，不换算绝对阻塞毫秒。CPU累计包括任务被记账的缓存停顿及部分中断开销。', '']
for c in cases:assert abs(c['scenes'][8]['pixels_per_render']-384000)<1
labels=['\n## 同开 XIP 后仍落后的多标签场景\n','| 角度 | 实现 | 有效绘制/s | flush次数/帧 | 像素/帧 | UI＋专用worker CPU ms/帧 | SW CPU ms/帧 |','|---:|---|---:|---:|---:|---:|---:|']
for a in [0,90]:
 for impl in ['present','adapter']:
  z=lookup['on',impl,a]['scenes'][7];n=z['render'];ui=sum(t['us'] for t in z['tasks'] if t['name'] in ['benchmark','lvgl','present_service'])/n/1000
  labels.append(f"| {a} | {impl} | {z['render_per_s']:.2f} | {z['flush']/n:.2f} | {z['pixels_per_render']:.0f} | {ui:.2f} | {z['software_draw_ms_per_render']:.2f} |")
labels+=['','90°两边每帧像素量仅差约2%，present的UI及专用工作任务累计时间仍更高。这部分覆盖交接、状态维护、复制驱动及其缓存停顿，不能全部算作通知或锁本身的开销。原无诊断、同容量90°标签场景也存在present 29 vs adapter 41的LVGL分差；因此剩余问题并非只由本轮探针产生，但本轮有效绘制率不能替代原生产分数。','']
text=text.replace('## 应优先处理什么','\n'.join(focus+labels)+'\n## 应优先处理什么')
(O/'README.md').write_text(text)
# Build a single scrollable comparison report with adjacent values, no selectors.
def esc(v):return html.escape(str(v))
def table(headers,rows):return '<div class="wrap"><table><thead><tr>'+''.join('<th>'+esc(x)+'</th>' for x in headers)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+esc(x)+'</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table></div>'
style='body{font:15px/1.7 system-ui,sans-serif;color:#17314c;background:#f2f5f8;margin:0}main{max-width:1600px;margin:auto;padding:24px}section{background:white;border:1px solid #d6e0eb;border-radius:12px;padding:20px;margin:18px 0}.wrap{overflow:auto}table{border-collapse:collapse;width:100%}th,td{padding:8px 12px;border-bottom:1px solid #dbe3ed;text-align:right;white-space:nowrap}th:first-child,td:first-child{text-align:left}th{background:#eaf1f8}a{color:#1467ab}h1{font-size:26px}h2{font-size:21px}.muted{color:#64748b}'
parts=[f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>S31 RGB 全面定位</title><style>{style}</style><main><h1>S31 RGB · 访问路径、有效绘制与交接成本</h1><p>2026-11-09 · 单输入 tile · 0° 80KB／90° 48KB · 双方共享配置一致 · 8 项完整 16 场景 · 干净 IDF</p><p><a href="README.md">完整分析与修改优先级</a> · <a href="scenes.csv">本轮 CSV</a> · <a href="original-scenes.csv">旧五板逐场景 CSV</a> · <a href="../present-adapter-five-board-20261109/comparison-current.html">原五板总表</a></p><section><h2>这轮确认了什么</h2><p>综合整数分相同不等于逐场景性能相同。原旋转缓冲容量并不相等，LVGL FPS 包含空刷新，原 CPU 100% 没有有效 idle 统计。present 借用缓冲后仍保留额外像素存储：S31 0° FULL/DIRECT 约多一屏 750KiB，旋转后需区分逻辑画布及 binding 保底。</p><p>XIP 改变外部代码和只读数据的访问路径；状态机与任务交接仍存在。下面使用非空绘制和每帧停顿量核对，不能把硬件 miss 寄存器当作缺失次数或 miss 率。</p></section>']
rows=[]
for v in ['off','on']:
 for a in [0,90]:
  p=summary(lookup[v,'present',a]);q=summary(lookup[v,'adapter',a]);rows.append([v,a,*[f'{z[k]:.2f}' for k in ['grade','render_fps','panel_fps','empty_pct','stall_m_frame','sw_ms_frame'] for z in [p,q]]])
parts+=['<section><h2>诊断窗口总览 · 每个指标相邻比较 present / adapter</h2><p>“LVGL分”是完整测试的16场景等权整数均值；其余为略去场景首秒后的窗口加权统计，二者不可直接相减。有效绘制及提交越多越好，空刷新占比／每帧停顿及CPU耗时越少越好。提交成功不代表扫描完成。</p>',table(['XIP','角度','P LVGL分','A LVGL分','P 绘制/s','A 绘制/s','P 提交/s','A 提交/s','P 空刷%','A 空刷%','P I停顿M/帧','A I停顿M/帧','P SW CPU ms/帧','A SW CPU ms/帧'],rows),'</section>']
for v in ['off','on']:
 for a in [0,90]:
  rows=[]
  for i in range(16):
   p=lookup[v,'present',a]['scenes'][i];q=lookup[v,'adapter',a]['scenes'][i]
   rows.append([p['name'],*[f'{s[k]:.2f}' for k in ['render_per_s','empty_pct','i_stall_m_per_render','software_draw_ms_per_render','wait_ms_per_render','cpu_busy_pct'] for s in [p,q]],f'{p["present_workers_ms_per_render"]:.2f}'])
  parts+=[f'<section><h2>XIP {v} · {a}° · present / adapter 逐场景</h2><p>SW CPU 为两个软件绘制线程合计；P worker 为专用后台任务，adapter 的同类成本在 UI 内，不能理解为零。wait 是 LVGL FLUSH_WAIT 事件墙钟，present 还包含 begin/acquire 等待；不能直接当作相同意义的纯传输延迟。</p>',table(['场景','P 绘制/s','A 绘制/s','P 空刷%','A 空刷%','P I停顿M/帧','A I停顿M/帧','P SW ms/帧','A SW ms/帧','P wait ms/帧','A wait ms/帧','P 真CPU%','A 真CPU%','P worker CPU ms/帧'],rows),'</section>']
memory_rows=[[m['mode'],m['rotation']]+[f'{m[k]:.2f}' for k in ['present_internal_open_KiB','adapter_internal_open_KiB','present_psram_open_KiB','adapter_psram_open_KiB']] for m in mem]
parts+=['<section><h2>旧正式矩阵的打开增量 · KiB · 此表是内存成本，越少越好</h2><p>这是 before_open 到 after_open 的差值，便于定位保底像素、逻辑画布和 tile 成本；不等同静态 data/BSS/IRAM，也不覆盖 board init 的公共 panel framebuffer。原表各时点“剩余”依然越多越好。</p>',table(['模式','角度','P 内部增量','A 内部增量','P PSRAM增量','A PSRAM增量'],memory_rows),'</section>']
parts+=['<section><h2>结论边界</h2><p>所有成功样本计数未饱和；首轮失败和饱和记录单独保留，不纳入成功成绩。本轮为诊断版本，一轮测量，代码布局会受 instrumentation 影响；不替换原始矩阵，不将 DOUBLE_PARTIAL 的结果外推到其他模式。原固件完整恢复和独立校验通过；未重新做目视防撕裂验收。</p><p>官方计数口径：<a href="https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html">Cache Access Counters</a>。下一步应优先解决统计和保底存储，再依据逐场景数据减少单 tile 的交接成本。</p></section></main></html>']
(O/'comparison.html').write_text('\n'.join(parts))
for n in ['configuration-audit.json','production-source-proof.json','code-placement.json','idle-hook-audit.json','original-scenes.csv','events.jsonl','stage.json']:shutil.copy2(R/n,O/n)
# Archive diagnostic source patches and scripts only; no build, ELF, or flash binary.
sources=O/'evidence/sources';sources.mkdir(parents=True,exist_ok=True)
for v in ['off','on']:
 changes=[];base=Path('/tmp/present-s31-rgb-analysis-20261109/baseline/source');cur=R/v/'source'
 for p in sorted(cur.rglob('*')):
  if not p.is_file():continue
  rel=p.relative_to(cur);b=base/rel
  if p.suffix not in ['.c','.h','.txt','.py','.defaults'] and p.name!='CMakeLists.txt':continue
  t=p.read_text();oldtext=b.read_text() if b.exists() else ''
  if t!=oldtext:changes+=difflib.unified_diff(oldtext.splitlines(True),t.splitlines(True),fromfile='a/'+str(rel),tofile='b/'+str(rel))
 (sources/(v+'.patch')).write_text(''.join(changes).replace('\n \n','\n\n'))
p=R/'lvgl/demos/benchmark/lv_demo_benchmark.c';b=Path('/home/xuxin/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.6.0~1_7d824107/demos/benchmark/lv_demo_benchmark.c');(sources/'lvgl-hook.patch').write_text(''.join(difflib.unified_diff(b.read_text().splitlines(True),p.read_text().splitlines(True),fromfile='a/demos/benchmark/lv_demo_benchmark.c',tofile='b/demos/benchmark/lv_demo_benchmark.c')).replace('\n \n','\n\n'))
for script in ['setup_s31_deep.py','fix_s31_probe.py','run_s31_deep.py','resume_s31_deep.py','analyze_s31_static.py','parse_s31_deep.py','report_s31_deep.py','check_s31_deep.mjs']:shutil.copy2(Path('/tmp')/script,sources/script)
a=O/'evidence/attempt1';a.mkdir(parents=True,exist_ok=True)
for n in ['failure.txt','controller.log','events.jsonl','stage.json']:shutil.copy2(R/'attempt1'/n,a/n)
for n in ['probe.c','main.c','bridge_present.c']:
 shutil.copy2(R/'attempt1/source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main'/n,a/n)
for n in ['live.log','manifest.json','sdkconfig','bench_cases.h']:
 p=R/'attempt1/present'/n
 if n.endswith('.log'):(a/(n+'.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 else:shutil.copy2(p,a/n)
restore=O/'evidence/restore';restore.mkdir(parents=True,exist_ok=True)
for n in ['manifest.json','restored.json']:shutil.copy2(R/'off/s31-rgb/backup'/n,restore/n)
for n in ['restore.log','restore-verify.log']:(restore/(n+'.gz')).write_bytes(gzip.compress((R/'off/s31-rgb/backup'/n).read_bytes(),mtime=0))
task_ratios=[sum(t['us'] for t in z['tasks'])/(2*z['us']) for c in cases for z in c['scenes'].values()]
assert min(task_ratios)>0.99 and max(task_ratios)<1.01
validation=dict(task_runtime_to_two_core_wall_min=min(task_ratios),task_runtime_to_two_core_wall_max=max(task_ratios),passed=True,cases=len(cases),scenes=len(flat),single_input_tile=True,capacity_bytes={'0':80000,'90':48000},shared_sdkconfig_identical_within_pairs=True,cache_saturation_samples=0,original_firmware_restored=True,production_runtime_changed=False,first_attempt_failed_not_counted=True)
(O/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
(O/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(O)}\n' for p in sorted(O.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
print('Report',O,'cases',len(cases),'scenes',len(flat))
