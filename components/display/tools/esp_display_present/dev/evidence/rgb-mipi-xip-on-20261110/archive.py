from pathlib import Path
import json,hashlib,shutil,gzip,tarfile,difflib,subprocess,datetime
from zoneinfo import ZoneInfo
R=Path('/tmp/present-xip-on-comparison-20261110');P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');D=P/'dev/evidence/rgb-mipi-xip-on-20261110';D.mkdir(parents=True,exist_ok=True)
data=json.loads((R/'matrix-data.json').read_text());validation=json.loads((R/'validation.json').read_text());assert data['complete'] and validation['passed']
def cp(src,dst):dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
def gz(src,dst):dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(gzip.compress(src.read_bytes(),mtime=0))
for n in ['comparison.html','comparison.csv','matrix-data.json','plan.json','diagnostic-source.json','frozen-source.json','validation.json','browser-validation.json','desktop.png','mobile.png','hardware-policy.json','public-main-compile.json','runner-tests.json','test_address.c','report.py','validate.py','archive.py','run_board.py','prebuild_clean.py','prebuild_diag.py','setup_clean.py','setup_diag.py','check_page.mjs']:cp(R/n,D/n)
for board,b in data['boards'].items():
 cp(R/board/'controller.json',D/board/'controller.json')
 for cohort,pair in b['cohorts'].items():
  for impl,v in pair.items():
   root=Path(v['source']);dest=D/board/cohort/impl
   for n in ['manifest.json','results.json','bench_cases.h']:cp(root/n,dest/n)
   for n in ['live.log','sdkconfig','build.log']:gz(root/n,dest/(n+'.gz'))
   for action in ['flash','capture','report']:
    q=R/board/(root.name+'-'+action+'.log')
    if q.exists():gz(q,dest/(action+'.log.gz'))
   project=json.loads((root/'build/project_description.json').read_text());(dest/'actual-driver.json').write_text(json.dumps({'idf':project.get('idf_path'),'esp_lcd_dir':project['build_component_info']['esp_lcd']['dir'],'cohort':cohort,'firmware_sha256':v['manifest']['firmware_sha256']},indent=2)+'\n')
for q in R.glob('identify-*.log'):gz(q,D/'operations'/(q.name+'.gz'))
o=R/'driver-overlay';cp(o/'trace-manifest.json',D/'driver-observer/trace-manifest.json')
base=Path('/home/xuxin/esp_work/esp-idf-qspi/components/esp_lcd');patch=''
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
    q=tools/comp/rel
    if q.exists():tf.add(q,arcname=cohort+'/'+comp+'/'+rel)
  for rel in ['dev/tools','dev/boards','dev/test_apps','dev/tests']:
   q=tools/'esp_display_present'/rel
   if q.exists():tf.add(q,arcname=cohort+'/esp_display_present/'+rel,filter=lambda ti:None if '__pycache__' in ti.name or ti.name.endswith('.pyc') else ti)
prior=P/'dev/evidence/s3-rgb-xip-on-address-20261110';cp(prior/'findings.md',D/'prior-s3-direct-findings.md');old_data=json.loads((prior/'matrix-data.json').read_text());(D/'prior-s3-direct-address-events.json').write_text(json.dumps({'cohort':'prior independent XIP ON diagnostic, not current counters','events':old_data['address_events'],'prior_matrix_sha256':hashlib.sha256((prior/'matrix-data.json').read_bytes()).hexdigest()},ensure_ascii=False,indent=2)+'\n')
lines=['# RGB / MIPI XIP ON 对比（2026-11-10）','','[网页](comparison.html) · [CSV](comparison.csv) · [逐项数据/来源](matrix-data.json)','','## 范围与执行方式','','S3 RGB 480×480 / S31 Korvo RGB 800×480 / P4 MIPI DSI 1024×600。全部XIP/FETCH_INSTRUCTIONS/RODATA开启，按前述验证范围仅0°。FULL双/三缓冲、DIRECT、PARTIAL双/三缓冲、AUTO；NONE排除，adapter AUTO每板记−。没有运行XIP OFF；旧OFF记录不混当前比较。S3十行bounce（9600B×2），S31不使用bounce；两边各板panel都保留3张FB池，单输入tile，PARTIAL双方按逻辑宽×50行分配。S3当前present FB_IRAM开启，32KiB指令缓存/64KiB数据缓存；所有有效配置原件归档。','','每板先完整跑干净IDF的present六项、adapter五项；随后单独跑隔离驱动轻量诊断的同样11项。每一有效项从open开始，连续完整16个benchmark场景，关闭成功才计PASS；本轮没有截取半场景、拼接被中断项目，也没有把先前测试分数混入。性能33项+诊断33项，共1056个完整场景，另有6个不支持条目。不得把这两轮加起来称66项正式性能分数。','','## 数据定义','','网页帧率使用LVGL完整16场景综合分，不是屏幕Hz。正式帧率与内存只取干净驱动轮，诊断轮不替换它们；adapter旧transport计数0不作为零FPS证据。','','内存仅四个剩余值：before_open、after_open、RUN期间局部最低剩余、after_close；RAM/PSRAM分列，KiB。最低值取after_run的*_min_free，不是after_run的当前free。箭头均在present列，与adapter比较；剩余越多越好，RAM和PSRAM低水位不一定同时发生。输入缓冲容量运行时核对，驱动FB/旋转画布等架构实际分配并不人为填平。','','## 轻量检测语义与边界','','S3：独立读实际bounce源bb_fb_index，对照交还旧FB、取得可写FB、确认新FB及LVGL渲染开始时的地址。S31：读AXI预取描述符所属FB，修正GDMA全局组号1到AXI局部组号0的诊断转换；这代表DMA预取借用，不能冒充当前输出像素地址。P4：独立记录每次实际装载DMA链表所用的局部FB索引，生产者cur_fb_index的后续变化不会改这个快照；LLP只是上下文，未宣称等价实际逐像素扫描位置。三种检测口径在网页逐板说明。','','地址命中按检查点累计，同一次提前交接可能对应确认、绘制两条记录，不等于独立撕裂次数。累计计数不截断，最多保存前16条地址样本。只有明确监测点才有覆盖；零命中不等于所有CPU/DMA写入都安全。取不稳或无法映射的源另记unknown，不当作零异常。独立输入tile等池外缓冲单独计数，不自动当成错误。S31描述符检查命中需结合预取语义复核。没有新增assert、故障注入、像素指纹、控制修正、额外延迟、回调替换或IRAM/时钟优化。诊断本身仍有短临界区开销，所以独立于正式性能轮。','','RUN门控：RGB以VSYNC，DSI以现有帧DMA完成为边界，丢弃RUN入口跨阶段的首半帧。open/close不纳入；benchmark结束回调统计日志仍在RUN窗口内。S3补数EOF不足与既有自动DMA重启分列；S31/P4用原有硬件underrun中断观察，S3补数计数不适用于其他平台，不填假0。','','[前一轮S3 adapter DIRECT曾命中的两组地址冲突](prior-s3-direct-findings.md)单独保留，未混入本轮计数。生产交接逻辑未修；本轮若未复现不能称已修复。','','## 本轮检测记录','']
for b,obj in data['boards'].items():
 lines.append('### '+b+'\n')
 for impl in ['present','adapter']:
  hits=[];unknown=0
  for x in obj['cohorts']['diagnostic'][impl]['rows']:
   if x['status']!='PASS':continue
   a=x['address'];n=sum(a[k] for k in ['release_active','write_active','confirm_wrong']);unknown+=a['raced']
   if n:hits.append(x['mode']+': '+str(n)+'次地址命中')
  lines.append('- '+impl+'：'+('；'.join(hits) if hits else '本轮监测点未检出地址命中')+'；未知源 '+str(unknown)+' 次。')
 lines.append('')
lines+=['## 验证与归档','','验证源代码冻结、基础IDF干净、真实esp_lcd组件路径、全部XIP标志、双方CPU/cache/PSRAM/LVGL有效公共配置一致、驱动原有控制语句未删除或重排、消费方hook剥离后等同生产控制流、16个唯一场景、非空绘制、单tile与容量、局部低水位、非零观察覆盖。Host ASan/UBSan包含正常/冲突/未知/阶段门控/DSI非bounce与underrun计数。浏览器逐格验证330个数值及120个内存箭头，桌面/手机无整页横向溢出、无下拉框。详见validation.json。','','diagnostic-source.tar.gz包含两轮实际组件/测试源码与完整隔离esp_lcd，observer.patch对干净IDF；原始日志/SDK配置gzip、每套manifest/results/bench_cases和操作记录完整保留。无build、固件镜像、依赖缓存或flash备份。生产源码和基础IDF没有修改。按用户要求不恢复原固件，结束保留各板最后的XIP ON诊断固件。','','当前页面只包含本轮三块RGB/DSI板，其他GRAM/TE以及先前角度数据属于历史，不拿来填补当前矩阵。']
(D/'README.md').write_text('\n'.join(lines)+'\n')

import csv
all_events=[]
for board,b in data['boards'].items():
 for impl,v in b['cohorts']['diagnostic'].items():
  for row in v['rows']:
   for ev in row.get('address_events',[]):all_events.append({'board':board,'implementation':impl,**ev})
with (D/'handoff-events.csv').open('w') as f:
 cols=['board','implementation','mode','rotation','us','kind','given','actual_source','driver_target','position','request_seq','event_seq','probe_raw'];w=csv.DictWriter(f,fieldnames=cols);w.writeheader();w.writerows(all_events)
notes=['# 本轮交接地址记录','','[网页](comparison.html) · [全部样本CSV](handoff-events.csv)','','本轮所有测试都开启XIP；性能和内存来自独立干净驱动轮。下面仅为独立轻量诊断轮的样本，不将命中数当作撕裂次数。kind1交还、kind2取得可写/渲染开始、kind3确认。每项最多保存前16条样本，累计计数不截断。','']
for board,b in data['boards'].items():
 for impl,v in b['cohorts']['diagnostic'].items():
  for row in v['rows']:
   evs=row.get('address_events',[])
   if not evs:continue
   notes += ['## '+board+' / '+impl+' / '+row['mode'],'',data['methods'][board],'','| esp_timer µs | 检查点 | 交付地址 | 驱动源/已装载地址 | 请求目标 | request_seq | event_seq |','| ---: | ---: | --- | --- | --- | ---: | ---: |']
   for ev in evs:notes.append('| '+str(ev['us'])+' | '+str(ev['kind'])+' | '+ev['given']+' | '+ev['actual_source']+' | '+ev['driver_target']+' | '+str(ev['request_seq'])+' | '+str(ev['event_seq'])+' |')
   if board=='s3-rgb':notes += ['','本轮DOUBLE_FULL的两组样本呈现确认目标与实际bounce源不同，随后绘制目标等于仍在取数的旧FB。adapter的双缓冲等待以switch_seq变化判断完成，未将完成事件绑定到具体提交地址；本轮只记录，没有修改等待/回调/交换。DIRECT在本轮未命中，不推翻前一轮已抓到的DIRECT两组事件。']
   if board=='p4-dsi' and any(ev['request_seq']==0 for ev in evs):notes += ['','样本位于本模式首次提交之前（request_seq=0）。测试在同一panel持续扫描的情况下依次关闭/打开显示模式；adapter初始化pipeline时假定fb[0]是front、fb[1]是draw，而样本中的已装载帧和可写/绘制目标都为fb[1]（0x4821ca80）。应核对模式重新打开时的初始front基线。这里是已编程DMA帧快照，尚未用逐像素硬件地址证明屏幕撕裂，不能把它夸大为已确认的物理扫描错误。']
   notes.append('')
notes += ['## 解释边界','','present本轮已覆盖检查点未命中，S31两边未命中；零命中不是所有CPU/DMA内存写入的完整证明。S31预取描述符、P4装载DMA快照和S3实际bounce源三种口径分开解释。供数/underrun等计数详见网页，不与地址命中合并。前一轮XIP ON的S3 DIRECT证据单独保留，未混本轮计数。']
(D/'findings.md').write_text('\n'.join(notes)+'\n')

(D/'host-validation.log').write_text('PASS: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DPRESENT_TRACE_HOST test_address.c\nActual observer is included by test source; no board fault injection or board asserts.\n')
(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-rgb-mipi-xip-on-20261110');shutil.copytree(D,delivery,dirs_exist_ok=True)
for root in [D,delivery]:
 for line in (root/'SHA256SUMS').read_text().splitlines():
  s,rel=line.split('  ',1);assert hashlib.sha256((root/rel).read_bytes()).hexdigest()==s,rel
# Publish only the fully validated new cohort; preserve the previous mixed page.
canonical=P/'dev/evidence/five-board-regression-20261109/comparison-current.html';history=canonical.with_name('comparison-before-xip-on-20261110.html')
if not history.exists():cp(canonical,history)
page=(D/'comparison.html').read_text()
for rel in ['comparison.csv','matrix-data.json','README.md','findings.md','handoff-events.csv','prior-s3-direct-findings.md']:page=page.replace('href="'+rel+'"','href="../rgb-mipi-xip-on-20261110/'+rel+'"')
canonical.write_text(page)
(R/'archived.json').write_text(json.dumps({'success':True,'evidence':str(D),'delivery':str(delivery),'canonical':str(canonical),'sha256_verified_both':True,'published':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()},indent=2)+'\n')
print('ARCHIVED/PUBLISHED',D,flush=True)
