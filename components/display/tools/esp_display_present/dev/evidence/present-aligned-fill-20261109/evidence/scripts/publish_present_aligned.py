from pathlib import Path
import json,shutil,hashlib,subprocess,sys
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');D=C/'deliveries';O=D/'present-aligned-fill-20261109';M=D/'present-adapter-five-board-20261109';E=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/evidence')
assert json.loads((O/'validation.json').read_text())['originals_restored']
assert json.loads((O/'browser-validation.json').read_text())['passed']
assert json.loads((O/'file-validation.json').read_text())['restored_boards']==4
for n in ['README.md','validation.json','browser-validation.json','file-validation.json','unified-iram-validation.json']:
 p=M/n
 if p.exists() and not (M/(p.stem+'-before-alignment'+p.suffix)).exists():shutil.copy2(p,M/(p.stem+'-before-alignment'+p.suffix))
for n in ['comparison-current.html','comparison-current.json','comparison-current.csv','comparison-current.xlsx','browser-validation.json','file-validation.json','validation.json','buffer-verification.json','analysis-summary.json','ANALYSIS.md','aligned-raw-results.json','aligned-manifests.json']:
 shutil.copy2(O/n,M/n)
(M/'README.md').write_text('''# present / adapter 五板对比（2026-11-09 更新）

[同网页对照](comparison-current.html)：S3 IRAM开／关独立表，帧率、容量、四时点剩余内存均并排。

最新更新是[同容量FULL/PARTIAL重测](../present-aligned-fill-20261109/README.md)。122项有效成绩、6项跳过，含S3 IRAM开／关、S31 RGB、P4 DSI和BOX-3单tile；每项16场景。RGB/DSI不含NONE。DIRECT/AUTO、mosaico TE和GRAM双tile保留历史，表内标明，不能称全部为本轮重跑。

PARTIAL双方按逻辑宽×50行×RGB565核对：S3 48000B；S31 0/180°80000B、90/270°48000B；P4分别102400B/60000B。adapter仅在隔离测试副本改变分配表达式，交接/复制/同步及IDF未改。固定单输入tile，未启用双tile。FULL检查单张整帧容量；额外像素存储仍计入内存。

[结果分析](ANALYSIS.md)按FULL/PARTIAL列出组均值，详项、逐场景及内存仍在网页。剩余内存越多越好，CSV/JSON保留原始bytes。LVGL分数和非空绘制/s都不等于实际扫描完成率；CPU100%不作真实负载。

comparison-current.json/csv/xlsx为合并新旧数据，cohort明确区分。本目录aligned-raw-results.json、aligned-manifests.json、buffer-verification.json只涉及新测；原始raw-results.json/manifests.json、evidence及*-before-alignment文件保留历史，不代表最新重测。完整新日志、配置、恢复证明在相邻present-aligned-fill-20261109目录。最新validation/file-validation/browser-validation针对本轮更新，历史验证另存before-alignment。

四块重测板均恢复运行前整片固件并独立校验；mosaico本轮未操作。P4原固件含旧NONE用例，恢复后可能再次显示，与本轮排除NONE的矩阵区分。串口跑完不替代目视防撕裂验收。
''')
def sums(root):
 files=sorted(p for p in root.rglob('*') if p.is_file() and p!=root/'SHA256SUMS')
 (root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(root))+'\n' for p in files))
sums(O);sums(M)
if '--repo' in sys.argv:
 shutil.copytree(O,E/O.name,dirs_exist_ok=True)
 # The old evidence directory intentionally has a different basename than the delivery.
 target=E/'five-board-regression-20261109'
 for p in M.iterdir():
  if p.is_file():shutil.copy2(p,target/p.name)
 sums(target)
 readme=E.parent/'README.md';text=readme.read_text()
 heading='## FULL/PARTIAL 同容量重测（2026-11-09）'
 if heading not in text:
  intro='''## FULL/PARTIAL 同容量重测（2026-11-09）

[最新合并网页](evidence/five-board-regression-20261109/comparison-current.html) · [新测证据与口径](evidence/present-aligned-fill-20261109/README.md) · [分析](evidence/present-aligned-fill-20261109/ANALYSIS.md)。122项有效成绩、6项跳过，每项完整16场景。S3 IRAM开／关、S31 RGB、P4 DSI的FULL/PARTIAL四角度及BOX-3单tile更新；固定单输入tile，PARTIAL双方按逻辑宽×50行分配，运行时核对实际bytes。DIRECT/AUTO、mosaico TE、GRAM双tile保留历史并在网页标注。四块板原固件全部恢复并独立整片校验；生产交接代码及IDF未修改。测试工具默认使用隔离adapter分配overlay，不更改其checkout。

'''
  text=text.replace('## S31 RGB 单tile性能定位',intro+'## S31 RGB 单tile性能定位',1)
 text=text.replace('## 最新五板对照（2026-11-09）','## 首轮五板基线（历史记录，2026-11-09）')
 start=text.find('[报告与口径]');end=text.index('\n\n## 目录',start) if start>=0 else -1
 if start>=0:text=text[:start]+'''首轮生产源码为b607bb9a1，present108项、adapter73项通过，31项跳过；原始口径保留在[首轮说明](evidence/five-board-regression-20261109/README-before-alignment.md)。目前合并网页中的FULL/PARTIAL已经由上面的同容量重测替换，其他项保留历史，不能再将整页称为同一轮数据。五板首轮恢复记录仍保留。'''+text[end:]
 readme.write_text(text)
 print('Published repository evidence, README index and existing entry point')
else:print('Published delivery entry point')
