from pathlib import Path
import importlib.util,json,re,datetime,hashlib,gzip,shutil,tarfile,csv,subprocess
from zoneinfo import ZoneInfo
R=Path(__file__).parent;P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');OLD=Path('/tmp/present-xip-on-four-angle-20261110');O=P/'dev/evidence/rgb-mipi-xip-on-four-angle-20261110';D=R/'report';D.mkdir(exist_ok=True)
assert json.loads((R/'controller.json').read_text())['success']
m=json.loads((O/'matrix-data.json').read_text());original_sha=hashlib.sha256((O/'matrix-data.json').read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('runner',R/'clean-source/components/display/tools/esp_display_present/dev/tools/run.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
new_evidence=[]
for cohort,trace in [('clean',False),('diagnostic',True)]:
 v=R/'s3-rgb/benchmark'/('present-xip-on'+('-driver-trace' if trace else ''));text=(v/'live.log').read_text();parsed=runner.parse_log(text);result=json.loads((v/'results.json').read_text());assert result['complete'] and result['matrix_complete'] and result['buffers_verified'] and not result['errors'];assert len(parsed['rows'])==24
 addresses={};supplies={};events={};scenes={}
 for line in text.splitlines():
  if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
   x=dict(re.findall(r'(\w+)=([^ ]+)',line));x={k:int(z) if z.isdecimal() else z for k,z in x.items()};key=(x['mode'],x['rotation'])
   if line.startswith('RGB_ADDRESS '):addresses[key]=x
   elif line.startswith('RGB_SUPPLY '):supplies[key]=x
   else:events.setdefault(key,[]).append(x)
  a=re.search(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',line)
  if a:scenes.setdefault((a[1],int(a[2])),[]).append({'name':a[3],'fps':int(a[4])})
 for row in parsed['rows']:
  key=(row['mode'],row['rotation']);row['scene_scores']=scenes[key];assert row['status']=='PASS' and row['scenes']==16 and len(scenes[key])==16 and len({s['name'] for s in scenes[key]})==16
  if trace:
   row.update(address=addresses[key],supply=supplies[key],address_events=events.get(key,[]));a=row['address'];assert a['writes']>100 and a['releases']>100 and a['confirms']>100 and a['boundaries']>1000;assert row['supply']['expected_eofs']==48
 m['boards']['s3-rgb']['cohorts'][cohort]['present']={'rows':parsed['rows'],'complete':parsed['complete'],'errors':parsed['errors'],'manifest':json.loads((v/'manifest.json').read_text()),'result':result,'source':str(v)}
 new_evidence.append(str(v))
m['boards']['s3-rgb']['controller']=json.loads((R/'controller.json').read_text())
m.update(updated=datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),fresh_full_run=False,source_commit='88feac560 (S3 no-FB_IRAM); c71871f43 (reused references)',memory_accounting=json.loads((R/'memory-accounting.json').read_text()),round_note='S3 present：移除 FB_IRAM 后重新完整测试 24 项正式性能＋24 项独立诊断。adapter 及 S31/P4 沿用上一轮完整四角度对照，不宣称全部板卡重新跑测。',provenance={'reference_dataset_sha256':original_sha,'new_variants':new_evidence,'new_clean_cases':24,'new_diagnostic_cases':24,'reused_clean_cases':108,'reused_diagnostic_cases':108})
m['boards']['s3-rgb']['config']='10 行 bounce · 已移除整组 FB_IRAM 映射'
m['boards']['s3-rgb']['origin_note']='present 本次新测；adapter 沿用上一轮'
for b in ['s31-rgb','p4-dsi']:m['boards'][b]['origin_note']='沿用上一轮完整四角度成绩（本次映射移除只影响 S3）'
m['summary']={'clean_pass':132,'diagnostic_pass':132,'skip':24,'address_hits':sum(sum((r.get('address') or {}).get(k,0) for k in ['release_active','write_active','confirm_wrong'])for b in m['boards'].values() for v in b['cohorts']['diagnostic'].values() for r in v['rows']),'errors':[]};m['complete']=True
(D/'matrix-data.json').write_text(json.dumps(m,indent=2,ensure_ascii=False)+'\n');shutil.copy2(R/'memory-accounting.json',D/'memory-accounting.json')
def copy(s,d):d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s,d)
def gz(s,d):d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(gzip.compress(s.read_bytes(),mtime=0))
for board,b in m['boards'].items():
 for cohort,pair in b['cohorts'].items():
  for impl,v in pair.items():
   root=Path(v['source']);dst=D/board/cohort/impl
   for name in ['manifest.json','results.json','bench_cases.h']:copy(root/name,dst/name)
   for name in ['live.log','sdkconfig','build.log']:gz(root/name,dst/(name+'.gz'))
   if cohort=='clean':
    sections=subprocess.check_output(['readelf','-SW',str(root/'build/present_benchmark.elf')],text=True);(dst/'elf-sections.txt').write_text(sections)
    project=json.loads((root/'build/project_description.json').read_text());(dst/'linked-components.json').write_text(json.dumps(project['build_components'],indent=2)+'\n')
   manifest=v['manifest'];assert manifest['xip_mode']=='on'
   assert len(v['rows'])==24
   if board=='s3-rgb' and impl=='present':assert 'ESP_DISPLAY_PRESENT_FB_IRAM' not in (root/'sdkconfig').read_text()
# Static accounting and baseline proof is tied to the exact clean ELF hash.
for board,pair in m['memory_accounting']['boards'].items():
 for impl,a in pair.items():
  v=Path(m['boards'][board]['cohorts']['clean'][impl]['source']);assert a['elf_sha256']==hashlib.sha256((v/'build/present_benchmark.elf').read_bytes()).hexdigest();assert not a['jpeg_components'] and not a['jpeg_symbols']
  assert a['suite_baselines']['before_init']==json.loads((v/'results.json').read_text())['suite_memory']['before_init']
# Retain source and observer provenance without build directories or firmware.
for name in ['source-baseline.json','diagnostic-changes.json','hardware-policy.json','controller.json','check_dashboard.mjs','assemble.py','finish.py']:copy(R/name,D/name)
assert '100% tests passed, 0 tests failed out of 48' in (R/'host-validation.log').read_text()
gz(R/'host-validation.log',D/'host-validation.log.gz')
(D/'host-validation.json').write_text(json.dumps({'passed':True,'tests':48,'sanitize':'ASan/UBSan','log':'host-validation.log.gz'},indent=2)+'\n')
copy(O/'diagnostic-source.tar.gz',D/'reference-source.tar.gz')
copy(O/'driver-observer/observer.patch',D/'driver-observer/observer.patch');copy(OLD/'driver-overlay/trace-manifest.json',D/'driver-observer/trace-manifest.json')
with tarfile.open(D/'new-s3-source.tar.gz','w:gz',dereference=True) as tf:
 for kind in ['clean','diag']:
  base=R/(kind+'-source/components/display/tools')
  for comp in ['esp_display_present','esp_lv_present']:
   for rel in ['src','include','CMakeLists.txt','Kconfig','idf_component.yml']:
    path=base/comp/rel
    if path.exists():tf.add(path,arcname=kind+'/'+comp+'/'+rel)
   if comp=='esp_display_present':
    for rel in ['dev/test_apps','dev/boards','dev/tools']:
     path=base/comp/rel
     if path.exists():tf.add(path,arcname=kind+'/'+comp+'/'+rel,filter=lambda info:None if '__pycache__' in info.name or '/build/' in info.name else info)
copy(R/'source-change.patch',D/'source-change.patch')
cols=['board','mode','angle','implementation','clean_status','diagnostic_status','fps','internal_total','internal_before_open_free','internal_after_open_free','internal_run_min_free','internal_after_close_free','internal_after_open_used','internal_run_peak_used','psram_total','psram_before_open_free','psram_after_open_free','psram_run_min_free','psram_after_close_free','psram_after_open_used','psram_run_peak_used','elf_iram','elf_dram_static','address_hits','unknown','supply_short','supply_restarts','origin']
with (D/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=cols);w.writeheader()
 for board,b in m['boards'].items():
  for impl,v in b['cohorts']['clean'].items():
   for r in v['rows']:
    x={'board':board,'mode':r['mode'],'angle':r['rotation'],'implementation':impl,'clean_status':r['status'],'diagnostic_status':next(q['status']for q in b['cohorts']['diagnostic'][impl]['rows']if(q['mode'],q['rotation'])==(r['mode'],r['rotation'])),'origin':'new S3 no FB_IRAM' if board=='s3-rgb' and impl=='present' else 'reference four-angle 20261110'}
    if r['status']=='PASS':
     mem=r['memory'];a=m['memory_accounting']['boards'][board][impl];d=next(q for q in b['cohorts']['diagnostic'][impl]['rows']if(q['mode'],q['rotation'])==(r['mode'],r['rotation']));addr=d['address'];x.update(fps=r['lvgl_fps'],internal_total=mem['after_open']['internal_total'],internal_before_open_free=mem['before_open']['internal_free'],internal_after_open_free=mem['after_open']['internal_free'],internal_run_min_free=mem['after_run']['internal_min_free'],internal_after_close_free=mem['after_close']['internal_free'],internal_after_open_used=mem['after_open']['internal_total']-mem['after_open']['internal_free'],internal_run_peak_used=mem['after_run']['internal_total']-mem['after_run']['internal_min_free'],psram_total=mem['after_open']['psram_total'],psram_before_open_free=mem['before_open']['psram_free'],psram_run_min_free=mem['after_run']['psram_min_free'],psram_after_close_free=mem['after_close']['psram_free'],psram_run_peak_used=mem['after_run']['psram_total']-mem['after_run']['psram_min_free'],psram_after_open_free=mem['after_open']['psram_free'],psram_after_open_used=mem['after_open']['psram_total']-mem['after_open']['psram_free'],elf_iram=a['iram_bytes'],elf_dram_static=a['dram_static_bytes'],address_hits=sum(addr[k]for k in ['release_active','write_active','confirm_wrong']),unknown=addr['raced'],supply_short=d.get('supply',{}).get('measured_short','') if d.get('supply')else'',supply_restarts=d.get('supply',{}).get('measured_restarts','') if d.get('supply')else'')
    w.writerow(x)
newrows=m['boards']['s3-rgb']['cohorts']['diagnostic']['present']['rows'];counts={k:sum(r['address'][k]for r in newrows)for k in ['release_active','write_active','confirm_wrong','raced','releases','writes','confirms']};supply={k:sum(r['supply'][k]for r in newrows)for k in ['measured_short','measured_restarts','measured_frames']}
(D/'findings.md').write_text('# 本次 S3 重测\n\n24 项正式性能和 24 项独立诊断均完整运行四角度，每项 16 场景。正式帧率/内存与诊断轮分离。\n\n地址统计：\n```json\n'+json.dumps(counts,indent=2)+'\n```\n\n供数统计：\n```json\n'+json.dumps(supply,indent=2)+'\n```\n\n零地址命中仅表示已覆盖检查点未发现；供数不足与地址异常分别解释。adapter 及其他两板保留上一轮已捕获异常，未宣称修复。\n')
(D/'README.md').write_text('''# 移除 S3 FB_IRAM 后的对比汇总

[网页](comparison.html) · [CSV](comparison.csv) · [完整数据](matrix-data.json) · [检测结果](findings.md)

本次新测 S3 present 的 6 模式 × 4 角度：24 项干净 IDF 帧率/内存、24 项隔离驱动诊断，共 768 个完整场景。XIP ON，bounce 10 行，32 KiB 指令缓存。基础 IDF 干净，不恢复原固件。每项从打开到完整 16 场景结束并关闭成功。

adapter 及 S31/P4 沿用 rgb-mipi-xip-on-four-angle-20261110 的完整四角度记录；本次改动不影响这些构建。对比表合计 132 正式 / 132 诊断，有 108 / 108 项沿用，不称全部重新跑测。原始历史数据不改，来源和实际 ELF SHA 见 JSON，正式与诊断不混成绩。

## SRAM 更正

ELF IRAM 与普通 DRAM 静态段单列；排除 dummy 别名，避免重复计算。RTC / P4 SPM 不计入普通 SRAM 段。段小计不等于芯片物理总量，缓存、保留区、对齐还影响堆容量。统计工具、readelf 输出、实际链接组件、日志及配置均归档。

网页单位为 KiB，CSV/JSON 内存保留原始 bytes；CSV 明列正式与诊断状态，不支持的 adapter AUTO 为 SKIP。四个剩余值保留，剩余越多越好。可切换堆已用：实际堆容量 − 剩余，运行期取本项局部峰值，越少越好；静态段不在堆已用内。箭头数值始终 present−adapter，颜色按指标优劣判断。RAM 与 PSRAM 的最低点不保证同时发生。

这是整套测试固件的账目，包含驱动、LVGL、任务和应用，不是组件独占占用。adapter 的 LVGL 任务在 bench_init 创建，present 首次打开才初始化 LVGL；不能直接用每项打开增量比较总开销。两者共用测试主任务，但 present 由主任务 pump、adapter 自有 LVGL 任务，不能把任务差异误称为纯 core 占用。关闭仅注销本项显示对象，共用驱动池、adapter 任务和 LVGL 全局资源仍可保留，并非全系统释放。

最终六个干净 ELF 未链接 JPEG 解码组件/入口，不扣除假定的 20 KiB。声明可选依赖不等于最终镜像包含库。memory-accounting.json 保存实际 ELF 哈希、段大小、SUITE 初始化前后基线及检查结果。

## FULL 缓冲核对

当前测试双方在 bench_init 都固定创建 3 张驱动 FB，已计入打开前；不是每个模式按需创建。present DOUBLE_FULL 使用其中 2 张，第三张仍预留；TRIPLE_FULL 使用 3 张。

present 的 LVGL 绑定层另外无条件创建一张全屏 draw 回退画布，供 detach/错误处理等路径使用。正常 RGB565 FULL 零拷贝刷新借用 presenter 的 lease，并非每帧都先画此画布再 memcpy。该画布真实占用 PSRAM，当前并未消除；所以模式名的 2/3 指扫描 FB，不是所有分配的全屏像素块总数。

有软件旋转（90/180/270 度）或字节序转换时，FB backend 还创建 1 张共享逻辑画布用于转换，区别于前述绑定层回退画布。当前 FULL 分配核对：

| 场景 | 驱动池已分配 | presenter 使用的原生 FB | backend 额外逻辑画布 | LVGL 绑定额外回退画布 |
| --- | ---: | ---: | ---: | ---: |
| DOUBLE_FULL 0 度 | 3 | 2（来自左列） | 0 | 1 |
| TRIPLE_FULL 0 度 | 3 | 3（来自左列） | 0 | 1 |
| DOUBLE_FULL 90/180/270 度 | 3 | 2（来自左列） | 1 | 1 |
| TRIPLE_FULL 90/180/270 度 | 3 | 3（来自左列） | 1 | 1 |

原生 FB 列是驱动池子集，不能两列相加。三板每张 RGB565 全屏分别为 S3 450 KiB、S31 750 KiB、P4 1200 KiB；不含管理结构和对齐。adapter 的旋转路径还会将已有 panel FB 用作绘图源，不能只看 double/triple 名称就断言双方图像池数量相同。这里解释现有分配，本次未同时改动缓冲生命周期，避免混淆 IRAM 前后帧率对照。

## 复现与归档

collect_memory_accounting.py 读取实际 build/present_benchmark.elf、project_description.json、live.log，随后 render_comparison.py 从汇总 JSON 生成网页。源码补丁与新测源码快照、原对照源码快照、隔离 observer 均归档。不含 build、固件、依赖缓存和 flash 备份。硬件性能只用于已测配置，零交接命中不能保证所有视觉现象被覆盖。
''')
previous=json.loads((O/'matrix-data.json').read_text())['boards']['s3-rgb']['cohorts']
previous_rows=previous['diagnostic']['present']['rows'];previous_short=sum(r['supply']['measured_short']for r in previous_rows);previous_frames=sum(r['supply']['measured_frames']for r in previous_rows)
comparison='\n## 与上一完整轮的 S3 present 对照\n\n| 指标 | 上一轮 FB_IRAM ON | 本次移除映射 |\n| --- | ---: | ---: |\n'
comparison+=f"| 独立观察轮供数不足 / RUN 帧 | {previous_short}/{previous_frames} | {supply['measured_short']}/{supply['measured_frames']} |\n"
comparison+='\n两次都是独立诊断轮，不能据此推算正式轮的供数不足或实际撕裂次数。该差异不能全部归因为单一因素；两轮均有供数不足，地址未命中不等于供数正常。上一轮完整来源 SHA256：'+original_sha+'。\n\n| 模式 | 上一轮正式分（0/90/180/270 度） | 本次正式分（0/90/180/270 度） |\n| --- | --- | --- |\n'
for mode in dict.fromkeys(r['mode']for r in m['boards']['s3-rgb']['cohorts']['clean']['present']['rows']):
 old_scores=' / '.join(str(r['lvgl_fps'])for r in previous['clean']['present']['rows']if r['mode']==mode);new_scores=' / '.join(str(r['lvgl_fps'])for r in m['boards']['s3-rgb']['cohorts']['clean']['present']['rows']if r['mode']==mode);comparison+=f'| {mode} | {old_scores} | {new_scores} |\n'
with (D/'findings.md').open('a')as f:f.write(comparison)
for name in ['collect_memory_accounting.py','render_comparison.py','comparison_dashboard.html']:copy(P/'dev/tools'/name,D/name)
print(D)
