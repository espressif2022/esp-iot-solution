from pathlib import Path
import json,hashlib,shutil,gzip,tarfile,difflib,csv,datetime
from zoneinfo import ZoneInfo
R=Path(__file__).parent;P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');NAME='rgb-mipi-xip-on-four-angle-20261110';D=P/'dev/evidence'/NAME
M=json.loads((R/'matrix-data.json').read_text());V=json.loads((R/'validation.json').read_text());assert M['complete'] and V['passed'];D.mkdir(parents=True,exist_ok=True)
def cp(s,d):d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s,d)
def gz(s,d):d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(gzip.compress(s.read_bytes(),mtime=0))
with (R/'TASK_STATE.md').open('a') as f:f.write('\nCompleted and independently validated all 132 clean + 132 diagnostic cases, all four angles, 4224 full scenes, 24 AUTO skips. See validation.json and findings.md for actual anomaly/unknown counts; completion does not mean no anomalies.\n')
for n in ['comparison.html','comparison.csv','matrix-data.json','plan.json','diagnostic-source.json','frozen-source.json','validation.json','browser-validation.json','desktop.png','mobile.png','hardware-policy.json','test_address.c','host-validation.log','host-validation.json','clean-validation.json','validation-adjustment.json','report.py','validate.py','archive.py','run_board.py','prebuild_clean.py','prebuild_diag.py','check_page.mjs','TASK_STATE.md']:cp(R/n,D/n)
for board,b in M['boards'].items():
 cp(R/board/'controller.json',D/board/'controller.json')
 for cohort,pair in b['cohorts'].items():
  for impl,v in pair.items():
   root=Path(v['source']);dst=D/board/cohort/impl
   for n in ['manifest.json','results.json','bench_cases.h']:cp(root/n,dst/n)
   for n in ['live.log','sdkconfig','build.log']:gz(root/n,dst/(n+'.gz'))
   for action in ['flash','capture','report']:
    q=R/board/(root.name+'-'+action+'.log')
    if q.exists():gz(q,dst/(action+'.log.gz'))
   project=json.loads((root/'build/project_description.json').read_text());(dst/'actual-driver.json').write_text(json.dumps({'idf':project.get('idf_path'),'esp_lcd_dir':project['build_component_info']['esp_lcd']['dir'],'cohort':cohort,'firmware_sha256':v['manifest']['firmware_sha256']},indent=2)+'\n')
 for q in (R/board).glob('*failure*'):
  if q.is_file():cp(q,D/board/'failures'/q.name)
o=R/'driver-overlay';cp(o/'trace-manifest.json',D/'driver-observer/trace-manifest.json');base=Path('/home/xuxin/esp_work/esp-idf-qspi/components/esp_lcd');patch=''
for rel in ['CMakeLists.txt','rgb/esp_lcd_panel_rgb.c','dsi/esp_lcd_panel_dpi.c','src/esp_lcd_present_trace.c','include/esp_lcd_present_trace.h']:
 old=(base/rel).read_text().splitlines(True) if (base/rel).exists() else [];new=(o/'esp_lcd'/rel).read_text().splitlines(True);patch+=''.join(difflib.unified_diff(old,new,fromfile='a/'+rel if old else '/dev/null',tofile='b/'+rel))
(D/'driver-observer/observer.patch').write_text(patch)
with tarfile.open(D/'diagnostic-source.tar.gz','w:gz',dereference=True) as tf:
 for f in sorted((o/'esp_lcd').rglob('*')):
  if f.is_file():tf.add(f,arcname='driver-overlay/esp_lcd/'+str(f.relative_to(o/'esp_lcd')))
 tf.add(o/'trace-manifest.json',arcname='driver-overlay/trace-manifest.json')
 for cohort in ['clean','diag']:
  tools=Path('/tmp/present-xip-on-'+cohort+'-source-20261110/components/display/tools')
  for comp in ['esp_display_present','esp_lvgl_adapter','esp_lv_present']:
   for rel in ['src','include','CMakeLists.txt','Kconfig','Kconfig.projbuild','idf_component.yml']:
    f=tools/comp/rel
    if f.exists():tf.add(f,arcname=cohort+'/'+comp+'/'+rel)
  for rel in ['dev/tools','dev/boards','dev/test_apps','dev/tests']:
   f=tools/'esp_display_present'/rel
   if f.exists():tf.add(f,arcname=cohort+'/esp_display_present/'+rel,filter=lambda ti:None if '__pycache__' in ti.name or ti.name.endswith('.pyc') else ti)
events=[];notes=['# 四角度 XIP ON 交接诊断结果','','本轮为全新完整四角度运行；不合并上一轮0°或XIP OFF数据。地址命中按检查点累计，同次提前交接可能对应多条记录，不代表独立撕裂次数。kind1交还、kind2取得可写/渲染开始、kind3确认。每项仅保存前16条样本，累计计数不截断。','','[网页](comparison.html) · [原始样本CSV](handoff-events.csv)','']
for board,b in M['boards'].items():
 notes+=['## '+board,'',M['methods'][board],'']
 for impl,v in b['cohorts']['diagnostic'].items():
  affected=[];unknown=0;short=0;frames=0;restarts=0;underruns=0
  for x in v['rows']:
   if x['status']!='PASS':continue
   a=x['address'];n=sum(a[k] for k in ['release_active','write_active','confirm_wrong']);unknown+=a['raced'];underruns+=a['underruns'];sup=x.get('supply')
   if sup:short+=sup['measured_short'];frames+=sup['measured_frames'];restarts+=sup['measured_restarts']
   if n:affected.append(x['mode']+' '+str(x['rotation'])+'°: '+str(n)+'次')
   for e in x.get('address_events',[]):events.append({'board':board,'implementation':impl,**e})
  notes.append('- '+impl+'：'+('；'.join(affected) if affected else '本轮已覆盖检查点未检出地址命中')+'；未知源 '+str(unknown)+' 次。')
  if board=='s3-rgb':notes.append('  RUN供数不足 '+str(short)+'/'+str(frames)+' 帧；既有自动重启 '+str(restarts)+' 次。')
  else:notes.append('  RUN硬件underrun中断 '+str(underruns)+' 次。')
 notes.append('')
notes+=['## 原始地址样本','','| 板卡 | 实现 | 模式 | 角度 | µs | kind | 交付地址 | 实际源/装载快照 | 请求目标 | request_seq | event_seq |','| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | ---: | ---: |']
for e in events:notes.append('| '+' | '.join(str(e[k]) for k in ['board','implementation','mode','rotation','us','kind','given','actual_source','driver_target','request_seq','event_seq'])+' |')
notes+=['','## 本轮样本分类','','S3 adapter DOUBLE_FULL 0°和DOUBLE_DIRECT 0°各出现一组配对：确认的新地址与当时实际bounce源不同，随后LVGL绘制的旧FB仍等于实际取数源。每组两条检查点记录，共两组，不能算四次独立撕裂。','','P4 adapter的6条记录不能全部归为初始化：DOUBLE_FULL 90°、TRIPLE_FULL 0°/90°三条request_seq=0，发生在本模式首次提交前；DOUBLE_DIRECT 0°在request_seq=1854出现确认/随后可写两条配对；TRIPLE_PARTIAL 0°在request_seq=1888交还的地址仍等于已编程DMA帧快照。后两组属于运行期地址交接风险。P4证据口径仍是已装载帧快照，不宣称取得逐像素扫描证明。','','S31 present的unknown分别位于TRIPLE_PARTIAL 180°和AUTO 270°，各1次。它们未能稳定映射源地址，不能当作正常或已确认冲突。','','S3供数不足累计present150/100630帧、adapter96/83827帧；两边覆盖模式数量不同（present24项、adapter20项），请按网页同模式同角度比较，不直接用两个累计次数判断优劣。均包含RUN结束统计日志窗口，且只来自独立诊断轮。']
notes+=['','## 解释限制','','S3实际bounce源、S31预取描述符、P4实际装载帧快照分开解释；后两者不是逐像素扫描证据。request_seq=0的样本发生在本模式首次提交之前，应核对共享panel重新打开时的初始front基线。仅记录，没有修复、assert或故障注入。零命中不能证明任意CPU/DMA写入安全；未知单列，不当作正常。前一轮0°记录保留在相邻rgb-mipi-xip-on-20261110历史目录，不混本轮计数。']
(D/'findings.md').write_text('\n'.join(notes)+'\n')
with (D/'handoff-events.csv').open('w') as f:
 cols=['board','implementation','mode','rotation','us','kind','given','actual_source','driver_target','position','request_seq','event_seq','probe_raw'];w=csv.DictWriter(f,fieldnames=cols);w.writeheader();w.writerows(events)
readme='''# RGB / MIPI · XIP ON · 四角度正式对比

[网页](comparison.html) · [CSV](comparison.csv) · [完整来源](matrix-data.json) · [交接结果](findings.md)

## 执行范围

本轮重新完整运行0°、90°、180°、270°，不复用上一轮0°成绩。S3 RGB 480×480、S31 Korvo RGB 800×480、P4 MIPI DSI 1024×600；present六种防撕裂模式，adapter五种支持模式。NONE排除，adapter AUTO为−。仅XIP/FETCH_INSTRUCTIONS/RODATA开启；没有XIP OFF数据。

每板先跑干净IDF的present24项、adapter20项，随后独立跑同样44项轻量交接诊断。每一有效项从open开始，连续完整16个benchmark场景，关闭成功才计PASS。正式性能132项，诊断132项，共4224个完整场景；另有24个不支持条目。两个cohort不混成绩，不把264项都称为正式性能成绩。完整controller、manifest、case表、SDK配置、原始日志均归档。

源码运行时部分与上一轮验证版本一致，源版本c71871f433e34dc456970fb99702362b9a34f213。只增加测试角度与报告，没有生产交接修复。S3 bounce10行，S31不使用bounce；双方panel保留3张FB池，输入tile1张，PARTIAL按逻辑宽×50行分配（90/270°按旋转后宽度）。S3 present FB_IRAM开启，32KiB指令/64KiB数据缓存。双方及clean/diagnostic各板CPU/cache/PSRAM/LVGL公共有效配置验证一致。

## 数据定义

帧率取LVGL完整16场景综合分，不等于屏幕Hz；adapter旧transport计数0不作为零FPS证据。正式帧率、内存只取干净驱动轮；交接检测来自独立诊断轮。

内存仅四个剩余值：打开前、打开后、运行最小、关闭后；RAM/PSRAM分列，单位KiB。运行最小取after_run的*_min_free，而非当前free；两种堆的低点不一定同时发生。present列箭头相对adapter，帧率和剩余内存均越多越好。输入容量运行时核对，架构实际FB/旋转画布分配不人为填平。

## 检测语义与验证

S3读实际bb_fb_index；S31读AXI预取描述符所属FB，使用GDMA全局到AXI局部组号转换；P4独立保存每次实际装载的DMA帧索引，生产者目标后续变化不改这个快照。检查交还、取得可写、确认与LVGL渲染开始。adapter仅0°双缓冲FULL/DIRECT走独立确认等待；90/180/270°改走pipeline交还，验证要求有正数release覆盖，对应独立confirm标不适用，不能将0/0当成覆盖通过。后两种硬件观察不能冒充逐像素输出证据。unknown与池外独立输入另列，不能当作无异常。

RUN阶段门控以RGB VSYNC、DSI现有帧DMA完成，丢弃入口首半帧；不含open/close，包含benchmark结束统计日志。S3供数不足及既有自动重启、S31/P4硬件underrun分别计数。不添加故障、不assert、不控制修正、不额外等待、不替换回调、不算像素指纹。诊断有额外开销，独立于正式成绩。零命中不表示所有CPU/DMA写入与肉眼现象均被覆盖。

验证源码冻结、基础IDF干净、实际esp_lcd组件路径、全部XIP标志、有效公共配置、驱动原控制语句未删除/重排、消费方hook剥离后等同生产控制流、四角度case全集、16个唯一场景、非空绘制、tile容量、局部低水位及非零观察覆盖。ASan/UBSan主机测试验证正常/冲突/未知/阶段门控。浏览器独立核对1320个数值和480个内存箭头，无下拉框、手机/桌面无整页横向溢出。完整结果见validation.json。

## 归档

diagnostic-source.tar.gz为两轮实际组件/测试源码和完整隔离esp_lcd；observer.patch基于干净IDF。归档不含build、固件镜像、依赖缓存、flash备份。基础IDF与生产交接源码均未修改。按用户要求不恢复原固件，保留最后运行的XIP ON诊断固件。

上一轮0°证据单独保留；历史中的已捕获冲突不会因本轮未复现而视为已修复。
'''
(D/'README.md').write_text(readme)
(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-rgb-mipi-xip-on-four-angle-20261110');shutil.copytree(D,delivery,dirs_exist_ok=True)
for root in [D,delivery]:
 for line in (root/'SHA256SUMS').read_text().splitlines():
  sha,rel=line.split('  ',1);assert hashlib.sha256((root/rel).read_bytes()).hexdigest()==sha,rel
canonical=P/'dev/evidence/five-board-regression-20261109/comparison-current.html';history=canonical.with_name('comparison-before-four-angle-20261110.html')
if not history.exists():cp(canonical,history)
page=(D/'comparison.html').read_text()
for rel in ['comparison.csv','matrix-data.json','README.md','findings.md','handoff-events.csv']:page=page.replace('href="'+rel+'"','href="../'+NAME+'/'+rel+'"')
canonical.write_text(page)
(R/'archived.json').write_text(json.dumps({'success':True,'evidence':str(D),'delivery':str(delivery),'canonical':str(canonical),'sha256_verified_both':True,'published':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()},indent=2)+'\n');print('ARCHIVED/PUBLISHED',D,flush=True)
