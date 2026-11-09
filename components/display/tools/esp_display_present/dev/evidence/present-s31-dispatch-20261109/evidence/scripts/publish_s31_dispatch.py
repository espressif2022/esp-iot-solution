from pathlib import Path
import json,hashlib,shutil,sys,re
C=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai');D=C/'deliveries';O=D/'present-s31-dispatch-20261109';M=D/'present-adapter-five-board-20261109';E=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/evidence')
assert json.loads((O/'root-validation.json').read_text())['passed'];assert json.loads((O/'root-browser-validation.json').read_text())['passed']
assert json.loads((O/'validation.json').read_text())['restored'];assert json.loads((O/'browser-validation.json').read_text())['passed'];assert json.loads((O/'file-validation.json').read_text())['passed']
def sums(root):
 files=sorted(p for p in root.rglob('*') if p.is_file() and p!=root/'SHA256SUMS')
 (root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(root))+'\n' for p in files))
# Do not alter formal scores or memory values; add a link to the independent diagnostic.
for root in [M]+([E/'five-board-regression-20261109'] if '--repo' in sys.argv else []):
 p=root/'comparison-current.html';s=p.read_text();tag='id="s31-dispatch-note"'
 if tag not in s:s=s.replace('<footer>','<section id="s31-dispatch-note" class="scope"><b>S31取指停顿进一步定位</b><p>24项完整16场景诊断与38项定点验证：真实补入／停顿、PC字体热点、24字节链接扰动及约1KiB字体解包IRAM验证；不覆盖本页正式分数。<a href="../present-s31-dispatch-20261109/root-cause.html">根因定位与完整证据</a></p></section><footer>',1)
 p.write_text(s)
 p=root/'README.md';s=p.read_text()
 if '../present-s31-dispatch-20261109/root-cause.html' not in s:s+='\n## S31 取指停顿与派发定位\n\n[根因定位](../present-s31-dispatch-20261109/root-cause.html)：24项完整16场景及38项定点运行，固定单tile、同容量；验证代码排列对字体解包取指缓存的影响，记录PC热点与约1KiB单函数IRAM对照。原固件已恢复并整片校验。本页原性能和内存矩阵不替换。\n'
 p.write_text(s);sums(root)
# Preserve reproduction scripts and validators in the evidence archive.
for n in ['publish_s31_dispatch.py','finish_s31_dispatch.py','check_published_s31_dispatch.py']:
 p=O/'evidence/scripts'/n;shutil.copy2('/tmp/'+n,p);p.chmod(0o644)
shutil.copy2(Path('/tmp/present-s31-dispatch-20261109/formal-data-guard.json'),O/'formal-data-guard.json')
sums(O)
if '--repo' in sys.argv:
 shutil.copytree(O,E/O.name,dirs_exist_ok=True)
 p=E.parent/'README.md';s=p.read_text();heading='## S31 取指停顿与pixel派发对照（2026-11-09）'
 if heading not in s:
  intro=heading+'\n\n[根因定位与证据](evidence/present-s31-dispatch-20261109/root-cause.html)：24项完整16场景与38项定点运行，固定单tile及同容量。用真实补入、停顿、PC热点和24字节代码平移，定位LVGL字形解包的缓存布局敏感性；约1KiB单函数IRAM对照收敛本轮排列波动。pixel worker绕过未追回文字吞吐，全局32B对齐被反例否决。具体cache set替换序列未直接trace。原固件已恢复并整片校验，生产源码和IDF未修改；正式帧率与内存数据未替换。\n\n'
  s=s.replace('## FULL/PARTIAL 同容量重测',intro+'## FULL/PARTIAL 同容量重测',1)
 p.write_text(s)
 print('Repository evidence and indexes updated')
else:print('Delivery evidence and entry point updated')
