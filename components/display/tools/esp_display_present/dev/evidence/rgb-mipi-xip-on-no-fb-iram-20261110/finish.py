from pathlib import Path
import subprocess,sys,json,time,hashlib,shutil,traceback,os
R=Path(__file__).parent;P=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');name='rgb-mipi-xip-on-no-fb-iram-20261110';evidence=P/'dev/evidence'/name;delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-'+name)
def run(cmd):subprocess.run(list(map(str,cmd)),check=True)
try:
 while True:
  state=json.loads((R/'controller.json').read_text())
  if state['stage']=='failed':raise RuntimeError(state)
  if state['success']:break
  time.sleep(10)
 run([sys.executable,R/'assemble.py']);D=R/'report'
 run([sys.executable,P/'dev/tools/render_comparison.py','--data',D/'matrix-data.json','--output',D/'comparison.html'])
 run(['/home/xuxin/.nvm/versions/node/v24.19.0/bin/node',R/'check_dashboard.mjs',D/'comparison.html',D,D/'matrix-data.json'])
 # All published entries are actual complete 16-scene results. Verify config,
 # image hash, observer coverage, raw data immutability, and the removed flag.
 m=json.loads((D/'matrix-data.json').read_text());v={'passed':True,'new_clean':24,'new_diagnostic':24,'new_scenes':768,'reused_clean':108,'reused_diagnostic':108,'angles':[0,90,180,270],'xip':'on','host_checks':48,'no_firmware_restore':True}
 base=Path('/tmp/present-xip-on-four-angle-20261110');oldraw=P/'dev/evidence/rgb-mipi-xip-on-four-angle-20261110/matrix-data.json';assert hashlib.sha256(oldraw.read_bytes()).hexdigest()==m['provenance']['reference_dataset_sha256']
 idf=Path('/home/xuxin/esp_work/esp-idf-qspi');assert not subprocess.check_output(['git','-C',str(idf),'status','--porcelain','--untracked-files=no'],text=True).strip()
 def common(s):return sorted(x for x in s.splitlines()if x.removeprefix('# ').startswith(('CONFIG_LV_','CONFIG_SPIRAM','CONFIG_ESP_DEFAULT_CPU_','CONFIG_COMPILER_OPTIMIZATION','CONFIG_FREERTOS_HZ','CONFIG_ESP32','CONFIG_CACHE_','CONFIG_LCD_')))
 for cohort in ['clean','diagnostic']:
  pair=m['boards']['s3-rgb']['cohorts'][cohort];configs=[]
  for impl,x in pair.items():
   root=Path(x['source']);sdk=(root/'sdkconfig').read_text();configs.append(common(sdk))
   assert all(flag in sdk for flag in ['CONFIG_SPIRAM_XIP_FROM_PSRAM=y','CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y'])
   result=x['result'];assert result['complete'] and result['buffers_verified'] and result['matrix_complete'] and not result['errors']
   assert len(x['rows'])==24
   for row in x['rows']:
    if row['status']=='SKIP':assert impl=='adapter' and row['mode']=='AUTO';continue
    assert row['status']=='PASS' and row['scenes']==16 and len(row['scene_scores'])==16
    if impl=='present':
     assert 'CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM' not in sdk
     assert row['buffer_audit']['elapsed_us']>60000000
     if cohort=='diagnostic':assert row['address']['writes']>100 and row['address']['boundaries']>1000
   proj=json.loads((root/'build/project_description.json').read_text());actual=Path(proj['build_component_info']['esp_lcd']['dir']).resolve();assert actual==(R/'driver-overlay/esp_lcd' if cohort=='diagnostic' else idf/'components/esp_lcd').resolve()
   assert hashlib.sha256((root/'build/present_benchmark.bin').read_bytes()).hexdigest()==x['manifest']['firmware_sha256']
  assert configs[0]==configs[1],cohort
 newtotal=m['boards']['s3-rgb']['cohorts']['clean']['present']['rows'][0]['memory']['after_open']['internal_total'];old=json.loads((oldraw).read_text())['boards']['s3-rgb']['cohorts']['clean']['present']['rows'][0]['memory']['after_open']['internal_total'];v['internal_heap_capacity_recovered_bytes']=newtotal-old
 v['browser']=json.loads((D/'validation.json').read_text());assert v['browser']['passed'];(D/'browser-validation.json').write_text(json.dumps(v['browser'],indent=2)+'\n');(D/'validation.json').write_text(json.dumps(v,indent=2)+'\n')
 (D/'SHA256SUMS').write_text(''.join(hashlib.sha256(f.read_bytes()).hexdigest()+'  '+str(f.relative_to(D))+'\n'for f in sorted(D.rglob('*'))if f.is_file() and f.name!='SHA256SUMS'))
 assert not evidence.exists();shutil.copytree(D,evidence);assert not delivery.exists();shutil.copytree(D,delivery)
 run([sys.executable,P/'dev/tools/render_comparison.py','--data',evidence/'matrix-data.json','--output',P/'dev/evidence/five-board-regression-20261109/comparison-current.html'])
 canonical=P/'dev/evidence/five-board-regression-20261109'
 readme=canonical/'README.md';text=readme.read_text();banner='> comparison-current.html 是动态入口，现指向移除 S3 FB_IRAM 后的 RGB/MIPI XIP ON 对比。本目录其余 CSV/JSON/XLSX 和下文均为 2026-11-09 历史记录；当前导出请用网页内链接。\n\n';readme.write_text(banner+text)
 sha=canonical/'SHA256SUMS';lines=sha.read_text().splitlines();lines=[hashlib.sha256((canonical/line.split('  ',1)[-1]).read_bytes()).hexdigest()+'  '+line.split('  ',1)[-1] if line.split('  ',1)[-1] in ['comparison-current.html','README.md'] else line for line in lines];sha.write_text('\n'.join(lines)+'\n')
 index=P/'dev/README.md';text=index.read_text();a=text.index('## RGB / MIPI XIP ON');z=text.index('## S31 取指停顿',a)
 current="""## RGB / MIPI XIP ON 对比（2026-11-10，当前数据）

[当前网页](evidence/five-board-regression-20261109/comparison-current.html) · [本次证据与 SRAM/缓冲说明](evidence/rgb-mipi-xip-on-no-fb-iram-20261110/README.md) · [诊断结果](evidence/rgb-mipi-xip-on-no-fb-iram-20261110/findings.md)。移除 S3 的 FB_IRAM Kconfig 和整组对象链接映射，保留显式 ISR/pixel IRAM_ATTR。

本次新跑 S3 present 6 模式 × 4 角度：24 项干净 IDF 正式性能/内存和 24 项隔离驱动诊断，共 768 场景。adapter、S31 RGB、P4 DSI 沿用上一完整四角度轮；汇总 132 正式 / 132 诊断，不宣称全部重新跑测。S3 十行 bounce，S31 非 bounce，全部 XIP ON；NONE 排除，adapter AUTO 为 −。

内存保留打开前、打开后、运行最小、关闭后四个剩余值；新增堆已用切换（实际堆容量−剩余，运行期取局部峰值）及 ELF 静态 SRAM/堆容量账目。剩余越多越好，已用/静态占用越少越好；差值在 present 列。它是整套测试固件的占用，不是组件独占。实际 ELF 未链接 JPEG 解码库，不扣除假定的 20 KiB。

FULL 的驱动池、模式使用的扫描 FB、绑定层全屏回退画布及旋转逻辑画布分开说明；当前额外画布未同时优化，避免混淆 IRAM 前后成绩。顶部 Tab 切换板卡，板内抽屉可收缩，内存和场景按模式选择。

诊断只记录、不控制修正，正式与诊断成绩分开；零命中不表示所有视觉异常被覆盖。基础 IDF 干净，交接控制流程未改，按要求不恢复原固件。

[上一完整四角度轮（历史）](evidence/rgb-mipi-xip-on-four-angle-20261110/README.md)及[更早 0° 轮](evidence/rgb-mipi-xip-on-20261110/README.md)原始证据保留不改。

"""
 index.write_text(text[:a]+current+text[z:])
 (R/'finished.json').write_text(json.dumps({'success':True,'evidence':str(evidence),'delivery':str(delivery),'validation':v},indent=2)+'\n');print('READY',evidence,delivery,flush=True)
except BaseException:
 (R/'finish-failure.txt').write_text(traceback.format_exc());raise
