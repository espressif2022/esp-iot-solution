from pathlib import Path
import json,shutil,gzip,tarfile,hashlib,subprocess,sys
R=Path(__file__).parent;P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');name='rgb-mipi-current-complete-20261111';D=P/'dev/evidence'/name
M=json.loads((R/'matrix-data.json').read_text());V=json.loads((R/'validation.json').read_text());assert M['complete'] and V['passed'];D.mkdir(parents=True,exist_ok=True)
def cp(s,d):d.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(s,d)
def gz(s,d):d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(gzip.compress(s.read_bytes(),mtime=0))
for n in ['matrix-data.json','memory-accounting.json','reuse-validation.json','normalized-defaults.json','source.json','frozen-source.json','validation.json','scope-correction.json','hardware-policy.json','diagnostic-source.json','consumer-observer.patch','missing_runner.py','supplement_board.py','assemble_current.py','validate_current.py','archive_current.py','p4_collect.py']:
 cp(R/n,D/n)
for board in M['boards']:
 cp(R/board/'supplement-controller.json',D/board/'controller.json')
 for cohort,trace in [('supplement-present',False),('diagnostic-present',True)]:
  src=R/board/'benchmark'/('present-xip-on'+('-driver-trace' if trace else ''));dst=D/board/cohort
  for n in ['manifest.json','results.json','bench_cases.h','variant.defaults']:
   if (src/n).exists():cp(src/n,dst/n)
  for n in ['sdkconfig','live.log','build.log']:gz(src/n,dst/(n+'.gz'))
  (dst/'elf-sections.txt').write_text(subprocess.check_output(['readelf','-SW',str(src/'build/present_benchmark.elf')],text=True))
  project=json.loads((src/'build/project_description.json').read_text());(dst/'linked-components.json').write_text(json.dumps({'build_components':project['build_components'],'lcd_component_path':project['build_component_info']['esp_lcd']['dir']},indent=2)+'\n')
 src=P/'dev/evidence/scanout-lifecycle-cleanup-20261111'/board;dst=D/board/'verified-present';dst.mkdir(parents=True,exist_ok=True)
 for f in src.iterdir():
  if f.is_file():cp(f,dst/f.name)
 for cohort in ['clean','diagnostic']:
  src=P/'dev/evidence/rgb-mipi-xip-on-no-fb-iram-20261110'/board/cohort/'adapter';shutil.copytree(src,D/board/(cohort+'-adapter'),dirs_exist_ok=True)
cp(R/'p4-dsi/case-ledger.json',D/'p4-dsi/case-ledger.json')
shutil.copytree(R/'p4-attempts',D/'p4-attempts',dirs_exist_ok=True)
if (R/'diagnostics').exists():shutil.copytree(R/'diagnostics',D/'diagnostics',dirs_exist_ok=True)
shutil.copytree(R/'driver-overlay',D/'driver-observer',dirs_exist_ok=True)
def source_filter(info):
 if '__pycache__' in Path(info.name).parts or info.name.endswith(('.pyc','.pyo')):return None
 return info
with tarfile.open(D/'clean-source.tar.gz','w:gz') as tar:tar.add(R/'clean-source/components',arcname='components',filter=source_filter)
with tarfile.open(D/'diagnostic-source.tar.gz','w:gz') as tar:tar.add(R/'diag-source/components',arcname='components',filter=source_filter)
notes=['# 当前源码交接与供数诊断','','只记录；正式帧率和内存不取诊断轮。adapter 已测项经运行源码、配置和观察点核对复用；present 是本次重新采集的完整诊断。零命中不证明任意条件下无撕裂，unknown 不当作正常。','']
for board,b in M['boards'].items():
 notes+=['## '+board,'',M['methods'][board],'']
 for impl,record in b['cohorts']['diagnostic'].items():
  unknown=hits=short=frames=restarts=under=0;affected=[]
  for row in record['rows']:
   if row['status']!='PASS':continue
   a=row['address'];n=sum(a[k] for k in ['release_active','write_active','confirm_wrong']);hits+=n;unknown+=a['raced'];under+=a['underruns'];s=row.get('supply')
   if s:short+=s['measured_short'];frames+=s['measured_frames'];restarts+=s['measured_restarts']
   if n:affected.append(f"{row['mode']} {row['rotation']}°：{n}")
  notes.append(f'- {impl}：地址命中 {hits}，unknown {unknown}，硬件 underrun {under}。'+('；'.join(affected) if affected else '已覆盖检查点无地址命中。'))
  if board=='s3-rgb':notes.append(f'  RUN 供数不足 {short}/{frames} 帧，既有自动重启 {restarts}。')
 notes.append('')
notes+=['检查点命中不等于独立撕裂次数。S3 检查实际 bounce 源；S31 为 AXI 预取所属 FB；P4 为实际装载帧快照，后两者不是逐像素输出证据。原始逐项计数与地址样本保留在 matrix-data.json。','', '补测 P4 曾在进入 app_main 前出现一次启动异常；原始日志单独保留在 diagnostics/p4-clean-startup-fault。相同固件复位后从首项重新开始；随后按 case-ledger 逐项保留实际完成与失败记录，未将中途失败的批次当作整套成功。启动异常未进入测例，不计分。']
(D/'findings.md').write_text('\n'.join(notes)+'\n')
(D/'README.md').write_text('''# RGB / MIPI 当前源码完整有效矩阵

[网页](comparison.html) · [当前表 JSON](comparison.json) · [CSV](comparison.csv) · [原始数据与 manifest](matrix-data.json) · [诊断说明](findings.md)

仅展示当前源码有效结果，不混入已失效旧版的 FULL 占位画布数据。范围：S3 RGB、S31 Korvo RGB、P4 DSI；六模式四角度。全部 XIP ON；S3 十行 bounce，S31 非 bounce；NONE 排除，adapter 不支持 AUTO 记横杠。

用户要求已测项不要重复：复用生产源码与公共配置一致的 present 24 项（每板 8 项），只补缺少的 present 48 项（每板连续完整 16 项）。adapter 的实际对齐源码、LVGL 场景、时钟/XIP/缓存配置、默认值、case 表和 ELF 静态 SRAM 与当前重新构建结果相同，因此复用已有 60 项。原始 manifest 保留实际采集来源；没有将复用改称本轮重跑。

正式矩阵共 132 项有效成绩，每项完整 16 场景并关闭成功。present 每板 8+16 两个完整采集批次，覆盖互不重叠；不是同一次连续 24 项跑测。四阶段内存保留各批次实际记录，不扣除或归一化首次 LVGL 全局初始化。ELF 静态 SRAM 和动态堆分列，它们属于整套固件，不是组件独占。

交接诊断独立于正式成绩：present 72 项新测，adapter 60 项核对一致后复用；共 132 项。两组累计 4224 个完整场景，24 个不支持条目。交接和供数只检测记录，不修正、不 assert，不注入故障；不能由零命中推导所有情况下无撕裂。失败尝试单独保存，不用其计分补表。

FULL 和原生 0° DIRECT 不再分配绑定层全屏占位画布。软件旋转 FULL 保留实际逻辑画布；旋转 DIRECT 保留实际绘制画布。S31 FULL 180°/270° 的打开 PSRAM 增量为约 751 KiB，不再采用失效版本约 1503 KiB 的数据。AUTO 依据实际池选三缓冲/双缓冲 PARTIAL，单 FB 拒绝。

正式轮使用干净 IDF；诊断轮只使用隔离 esp_lcd 副本，实际组件路径与观察覆盖有验证。clean-source.tar.gz / diagnostic-source.tar.gz 保存实际源码，consumer-observer.patch 与 driver-observer 保存只记录的改动。缺项过滤由 missing_runner.py 调用原 case planner 后排除已测 8 项，并记录实际生成 case 表。不改生产代码、IDF，不恢复原固件。没有 build、依赖缓存、固件镜像或 flash 备份目录。

validation.json 核对完整覆盖、原始记录、配置/容量、干净 IDF、观察点控制流以及源码不变性；reuse-validation.json 说明复用依据。网页数值与布局由浏览器另行校验，结果在 browser-validation.json。
''')
readme=D/'README.md';text=readme.read_text();summary=M['summary']
text=text.replace('正式矩阵共 132 项有效成绩，每项完整 16 场景并关闭成功。present 每板 8+16 两个完整采集批次，覆盖互不重叠；不是同一次连续 24 项跑测。', f"正式覆盖 132 个组合：{summary['clean_pass']} 项通过、{summary['clean_fail']} 项失败。只有通过项的完整 16 场景与成功关闭才计入分数。S3 为 8+16 两个完整批次；S31 复位中断后复用完整的 12 项，仅补余下 4 项；P4 按 case-ledger.json 逐项记录已完成与失败，复位后只采集未覆盖项。未将这些批次称为一次连续全通过的 24 项跑测。")
text=text.replace('交接诊断独立于正式成绩：present 72 项新测，adapter 60 项核对一致后复用；共 132 项。两组累计 4224 个完整场景，24 个不支持条目。', f"交接诊断独立于正式成绩：present 72 个组合新采集，adapter 60 项核对一致后复用；共 132 个组合，其中 {summary['diagnostic_pass']} 项通过、{summary['diagnostic_fail']} 项未完成。正式与诊断累计 {(summary['clean_pass']+summary['diagnostic_pass'])*16} 个有效完整场景，24 个不支持条目；失败尝试不计有效场景或 FPS。")
text+='\n`matrix-data.json` 的 complete / coverage_complete 表示各组合已有明确采集结果，不表示全部通过；全通过标志见 current_round.all_passed，失败由 status 和 summary.errors 明确记录。\n'
text+='\n不同采集批次的 case 表大小及 XIP 影像可能造成亚 KiB 的 PSRAM 基线差异。保留原始剩余值；打开增量以同一项的 before/after 相减，不把基线差异当成帧缓冲开销。诊断统计仅包含成功输出报告的组合，未完成项不当作零异常；S3 的既有重启是驱动扫描重启，不是芯片复位。\n'
readme.write_text(text)
with (D/'findings.md').open('a') as stream:
 stream.write('\n## 未成功完成的当前组合\n\n')
 for board,b in M['boards'].items():
  for cohort,pair in b['cohorts'].items():
   for impl,v in pair.items():
    for row in v['rows']:
     if row['status']=='FAIL':stream.write(f"- {board} / {impl} / {cohort} / {row['mode']} {row['rotation']}°：{row['failure_reason']}\n")
for name2 in ['render_comparison.py','comparison_dashboard.html','check_comparison.mjs']:cp(Path('/tmp/latest-'+name2),D/'presentation-tools'/name2)
subprocess.run([sys.executable,str(D/'presentation-tools/render_comparison.py'),'--data',str(D/'matrix-data.json'),'--output',str(D/'comparison.html')],check=True)
# Do not publish the canonical entry until the generated dashboard has passed browser validation.
(R/'archive-ready.json').write_text(json.dumps({'evidence':str(D),'success':True},indent=2)+'\n');print(D)
