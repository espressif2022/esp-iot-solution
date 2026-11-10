from pathlib import Path
import json,re,csv,html,sys,gzip,hashlib,shutil,difflib
r=Path('/tmp/present-s3-xip-on-address-20261110');modes=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];rows=[]
for impl in ['present','adapter']:
 v=r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace');text=(v/'live.log').read_text(errors='replace') if (v/'live.log').exists() else ''
 def records(prefix):
  d={}
  for line in text.splitlines():
   if line.startswith(prefix+' '):
    x=dict(re.findall(r'(\w+)=([^ ]+)',line));d[x['mode']]={k:int(value) if value.isdecimal() else value for k,value in x.items()}
  return d
 checks=records('RGB_ADDRESS');supplies=records('RGB_SUPPLY');passed=records('I_SKIP')
 for mode in modes:
  if impl=='adapter' and mode=='AUTO':rows.append({'implementation':impl,'mode':mode,'status':'SKIP'});continue
  pattern=r'PASS mode='+mode+r' te_buffers=\d+ rotation=0 frames=(\d+) transport_fps=([\d.]+) lvgl_fps=(\d+).*? scenes=(\d+)'
  matches=re.findall(pattern,text);x={'implementation':impl,'mode':mode,'status':'PASS' if matches else 'pending'}
  if matches:
   frames,transport,fps,scenes=matches[-1];x.update(frames=int(frames),transport_fps=float(transport),lvgl_fps=int(fps),scenes=int(scenes),address=checks.get(mode),supply=supplies.get(mode))
  rows.append(x)
complete=sum(x['status']=='PASS' for x in rows)==11
state=json.loads((r/'controller.json').read_text())
events=[]
for impl in ['present','adapter']:
 text=(r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace/live.log')).read_text(errors='replace')
 for line in text.splitlines():
  if line.startswith('RGB_ADDRESS_EVENT '):
   x=dict(re.findall(r'(\w+)=([^ ]+)',line));events.append({'implementation':impl,**{k:int(v) if v.isdecimal() else v for k,v in x.items()}})
data={'address_events':events,'complete':complete,'controller':state,'conditions':json.loads((r/'setup.json').read_text()),'rows':rows,'limits':['Live driver bb_fb_index is independently read at software ownership transitions, not compared after assignment.','Only explicit release/acquire/confirm and LVGL render-start boundaries covered; zero is not proof against arbitrary CPU/DMA writes or every visible fault.','Pool-external input tiles are counted but not errors. Cross-switch source read is separate. Source/target/position are not a fully atomic struct snapshot; target is context only.','Observer takes short critical sections, changes scheduling slightly; no pixel hashes, asserts, corrections, callback replacement or delays. Diagnostic FPS/memory not formal performance data.','RUN begins on next VSYNC, leading partial frame and close excluded; benchmark summary logging included.']}
(r/'matrix-data.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
fields=['implementation','mode','status','scenes','lvgl_fps','releases','writes','confirms','not_pool','raced','release_active','write_active','confirm_wrong','measured_frames','measured_short','measured_restarts']
with (r/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
 for x in rows:
  y={k:x[k] for k in fields if k in x}
  for obj in ['address','supply']:y.update({k:v for k,v in (x.get(obj) or {}).items() if k in fields and k not in ['mode','rotation']})
  w.writerow(y)
e=html.escape;body='''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S3 XIP ON buffer检查</title><style>body{font-family:system-ui;line-height:1.6;margin:24px;color:#203040}main{max-width:1100px;margin:auto}table{border-collapse:collapse;width:100%}td,th{padding:12px;border:1px solid #ccd1d7;text-align:left}small{display:block;color:#596875}.bad{background:#fff0e8}.ok{background:#f0f8f2}.wrap{overflow:auto}</style><main><h1>S3 RGB · XIP ON · 0° · buffer地址检查</h1>'''
body+='<p>'+('采集完成' if complete else '采集中')+'。16MHz PCLK、约60.33Hz、十行bounce、CPU240MHz、PSRAM80MHz。每个支持模式完整16场景；NONE排除。</p><div class="wrap"><table><tr><th>模式</th><th>present</th><th>adapter</th></tr>'
for mode in modes:
 body+='<tr><th>'+mode+'</th>'
 for impl in ['present','adapter']:
  x=next(x for x in rows if x['implementation']==impl and x['mode']==mode)
  if x['status']!='PASS':body+='<td>'+('−' if x['status']=='SKIP' else '待完成')+'</td>';continue
  a=x['address'];s=x['supply'];assert a and s
  body+='<td class="'+('bad' if a['release_active'] or a['write_active'] or a['confirm_wrong'] else 'ok')+'">交还仍在使用 '+(str(a['release_active'])+' / '+str(a['releases']) if a['releases'] else '−（此路径无交还钩子）')+'<small>取得可写时仍被读取 '+str(a['write_active'])+' / '+str(a['writes'])+'；确认新地址不符 '+(str(a['confirm_wrong'])+' / '+str(a['confirms']) if a['confirms'] else '−（此路径无确认钩子）')+'</small><small>池外缓冲 '+str(a['not_pool'])+'；源地址读取跨切换 '+str(a['raced'])+'</small><small>运行期供数不足 '+str(s['measured_short'])+' / '+str(s['measured_frames'])+' 帧；重启 '+str(s['measured_restarts'])+'</small><small>16场景；诊断LVGL FPS '+str(x['lvgl_fps'])+'</small></td>'

 body+='</tr>'
body+='</table></div><p>驱动提供实际bb_fb_index源地址；在present/adapter影子副本的交还旧缓冲、取得可写缓冲、确认新缓冲处检查，并在LVGL渲染开始检查绘制目标。kind1=交还，kind2=取得/绘制，kind3=确认。独立读取实际源，不使用赋值后天然相等的比较。</p><p>只记录，不assert、不修正或延迟控制。这里只检查明确的几个所有权边界，不是所有CPU/DMA内存写入的全覆盖监控；零值表示本轮监测点未检出。池外缓冲通常是独立输入tile，不当作错误；跨切换无法取稳源地址单独计数。源和target不是整个结构的原子快照，target只供上下文。</p><p>全量指纹版本因明显增负载被中止，仅保留DOUBLE_FULL0原始证据，不当成原程序异常率。此轻量版本不计算像素指纹。诊断FPS不替换正式性能表。<a href="findings.md">结论与地址证据</a>。<a href="comparison.csv">CSV</a> · <a href="matrix-data.json">JSON</a></p></main>'

(r/'comparison.html').write_text(body)
print(json.dumps({'complete':complete,'controller':state['stage'],'pass':sum(x['status']=='PASS' for x in rows),'address_alerts':sum(x.get('address',{}).get('release_active',0)+x.get('address',{}).get('write_active',0)+x.get('address',{}).get('confirm_wrong',0) for x in rows)},ensure_ascii=False))
if '--archive' not in sys.argv:sys.exit()
assert complete and state['success'] and state['stage']=='done'
for impl in ['present','adapter']:
 v=r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace');res=json.loads((v/'results.json').read_text());assert res['complete'] and res['matrix_complete'] and res['buffers_verified'] and not res['errors']
 sdk=(v/'sdkconfig').read_text()
 for flag in ['CONFIG_SPIRAM_XIP_FROM_PSRAM=y','CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y']:assert flag in sdk,flag
 for x in rows:
  if x['implementation']==impl and x['status']=='PASS':assert x['scenes']==16 and x['address']['writes']>100 and x['supply']['expected_eofs']==48
for x in rows:
 if x['status']!='PASS':continue
 a=x['address']
 assert a['raced']==0,('Unresolved source reads require explicit review',x)
 if x['implementation']=='present':assert a['releases']>100 and a['confirms']>100,x
 elif x['mode'] in ['DOUBLE_FULL','DOUBLE_DIRECT']:assert a['confirms']>100,x
 else:assert a['releases']>100,x
# Compare all effective common settings, including disabled flags.
prefixes=('CONFIG_LV_','CONFIG_SPIRAM','CONFIG_ESP_DEFAULT_CPU_','CONFIG_COMPILER_OPTIMIZATION','CONFIG_FREERTOS_HZ','CONFIG_ESP32','CONFIG_CACHE_','CONFIG_LCD_')
def common(impl):return sorted(l for l in (r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace/sdkconfig')).read_text().splitlines() if l.removeprefix('# ').startswith(prefixes))
assert common('present')==common('adapter'),list(difflib.unified_diff(common('present'),common('adapter')))
D=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present/dev/evidence/s3-rgb-xip-on-address-20261110');D.mkdir(parents=True,exist_ok=True)
for n in ['hook-validation.json','production-source-hashes.json','comparison.html','comparison.csv','matrix-data.json','setup.json','controller.json','hardware-policy.json','run.py','report.py','test_address.c'] : shutil.copy2(r/n,D/n)
for impl in ['present','adapter']:
 v=r/'s3-rgb/benchmark'/(impl+'-xip-on-driver-trace');d=D/impl;d.mkdir(exist_ok=True)
 for n in ['manifest.json','results.json','bench_cases.h']:shutil.copy2(v/n,d/n)
 for n in ['live.log','sdkconfig','build.log']:(d/(n+'.gz')).write_bytes(gzip.compress((v/n).read_bytes(),mtime=0))
 for action in ['prepare','flash','capture','report']:
  p=r/(impl+'-'+action+'.log')
  if p.exists():(d/(action+'.log.gz')).write_bytes(gzip.compress(p.read_bytes(),mtime=0))
o=r/'driver-overlay';d=D/'driver-observer';d.mkdir(exist_ok=True);shutil.copy2(o/'trace-manifest.json',d/'trace-manifest.json')
for rel in ['rgb/esp_lcd_panel_rgb.c','src/esp_lcd_present_trace.c','include/esp_lcd_present_trace.h']:
 p=o/'esp_lcd'/rel;dest=d/(rel+'.gz');dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
base=Path('/home/xuxin/esp_work/esp-idf-qspi/components/esp_lcd');patch=''
for rel in ['rgb/esp_lcd_panel_rgb.c','src/esp_lcd_present_trace.c','include/esp_lcd_present_trace.h']:
 old=(base/rel).read_text().splitlines(True) if (base/rel).exists() else [];new=(o/'esp_lcd'/rel).read_text().splitlines(True);patch+=''.join(difflib.unified_diff(old,new,fromfile='a/'+rel,tofile='b/'+rel))
(d/'rgb-observer.patch').write_text(patch)
(D/'README.md').write_text('# S3 XIP ON 轻量地址诊断\n\n[检测表](comparison.html) · [CSV](comparison.csv) · [JSON](matrix-data.json)\n\n0°，11项支持模式、每项完整16场景，共176场景。adapter AUTO不支持，NONE排除。PCLK16MHz，十行bounce，CPU240MHz，PSRAM80MHz，XIP/FETCH/RODATA开启。公共有效SDK配置逐项相同。\n\n'+ '\n\n'.join(data['limits'])+'\n\nHost ASan/UBSan验证：正常交还不报错；交还/取得仍在读的FB能检出；即使软件target匹配而实际source不同也能检出确认异常；错误池指针、跨切换未知值、阶段门控和reset区分记录。正式checkout和基础IDF不改，只有影子present/adapter和隔离esp_lcd观察副本。\n\n全量内容指纹版本产生明显供数负担，已停止。其首项完整DOUBLE_FULL0有2112/4201次不足，195804段指纹无变化，不能作为原程序错误率或无踩踏证明。同期轻量对照单项完整16场景另附；不是伪称整套跑完。\n\n按用户要求不恢复原固件，保留adapter XIP ON地址诊断固件。归档无build和固件镜像。\n')

assert len(events)==4 and all(x['mode']=='DOUBLE_DIRECT' and x['implementation']=='adapter' for x in events)
for i in [0,2]:
 assert events[i]['kind']==3 and events[i]['given']!=events[i]['actual_source'] and events[i]['given']==events[i]['driver_target']
 assert events[i+1]['kind']==2 and events[i+1]['given']==events[i+1]['actual_source']==events[i]['actual_source']
(D/'findings.md').write_text("""# 本轮发现：供数异常与地址冲突分别记录

S3 GC9503 480×480，16MHz PCLK（约60.33Hz），十行bounce，CPU240MHz、PSRAM80MHz，XIP/FETCH/RODATA开启。0°，present六项、adapter五项，每项完整16场景。生产代码和基础IDF保持原样，只运行隔离观察副本。

## 独立地址证据

present六项在已覆盖的交还、取得可写、确认与LVGL绘制开始检查点未命中地址冲突。adapter DOUBLE_DIRECT检出2次确认地址不符，紧接2次绘制目标仍为实际bounce源。其余四项未命中。不能把未命中推广为所有CPU/DMA写入均安全。

| 时刻（启动后µs） | 检查点 | 软件交付的地址 | 驱动实际bounce源 | 驱动当前请求目标 | bounce位置（像素） |
| ---: | --- | --- | --- | --- | ---: |
| 188532754 | 提交等待结束后的确认 | 0x3c0e09c0 | 0x3c151200 | 0x3c0e09c0 | 4800 |
| 188534026 | LVGL绘制开始 | 0x3c151200 | 0x3c151200 | 0x3c0e09c0 | 9600 |
| 189560357 | 提交等待结束后的确认 | 0x3c0e09c0 | 0x3c151200 | 0x3c0e09c0 | 0 |
| 189561030 | LVGL绘制开始 | 0x3c151200 | 0x3c151200 | 0x3c0e09c0 | 9600 |

这不是在驱动完成赋值后比较两个必然相同的字段，而是在消费方执行所有权交接时，独立读取驱动实际bb_fb_index。S3 bounce模式下DMA读的是内部bounce；这里追踪的是仍需被CPU取数的PSRAM framebuffer，不能提前拿来写。

代码风险位于adapter的display_bridge_v9_prepare_double_wait / arm_double_wait_after_submit / complete_double_wait_isr：序号变化被当作提交完成，但没有绑定实际取数buffer地址。驱动可能已产生一笔旧帧边界事件，而新cur_fb_index仍需等待下一轮bounce源切换。样本证明错误确认后出现可写目标与实际源重合；本轮未记录arm分支本身，不把具体分支时序宣称为已实测。没有改变等待、回调、buffer交换或自动重启行为，也没有加assert。

## 供数计数（完整RUN窗口，不含open/close）

| 模式 | present不足/重启 | adapter不足/重启 |
| --- | ---: | ---: |
| DOUBLE_FULL | 2 / 2 | 0 / 0 |
| TRIPLE_FULL | 3 / 3 | 8 / 8 |
| DOUBLE_DIRECT | 2 / 2 | 1 / 1 |
| DOUBLE_PARTIAL | 6 / 6 | 27 / 27 |
| TRIPLE_PARTIAL | 5 / 5 | 22 / 22 |
| AUTO | 8 / 8 | − |

每项约4184–4195个完整扫描周期。窗口包括benchmark结束时统计日志，不应将所有不足次数直接归属到画面内容绘制阶段。供数不足和地址冲突是两个独立检测项。

## 已弃用的中间诊断

用户反馈严重抖动时运行的是全量指纹版本。该版额外读取完整framebuffer并逐段计算指纹，导致DOUBLE_FULL 2112/4201次供数不足，LVGL18FPS。同期轻量对照完整16场景仅2/4193次、26FPS，最终地址观察版2/4195次、26FPS。指纹版明显干扰被测系统，已中止，不能用它的异常率评价原程序，也不能用其零内容变化证明没有踩踏。

第一版地址观察误取0°未使用的scan字段，导致交还/确认落入池外，未形成有效覆盖；已中止并保留原始记录。最终版改成与真实submit相同的scan_storage选择表达式，所有要求的检查路径均有正数样本。无效中间尝试不计入最终11项。

本轮完成后保留adapter XIP ON地址诊断固件，按用户要求未恢复原固件。没有修改正式性能表；诊断本身仍有轻微调度开销。
""")
validation={'supported_cases':11,'scenes':176,'angle':0,'host_asan_ubsan':'PASS','common_sdkconfig_identical':True,'xip_all_three_flags':True,'no_original_restore':True}
(D/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')

import tarfile,io
source=Path('/tmp/present-s3-on-address-source-20261110/components/display/tools')
with tarfile.open(D/'diagnostic-source.tar.gz','w:gz') as tf:
 for comp,rel in [('esp_display_present','src/backend/present_backend_fb.c'),('esp_display_present','dev/test_apps/benchmark/main/buffer_audit.c'),('esp_display_present','dev/test_apps/benchmark/main/main.c'),('esp_display_present','dev/tools/run.py'),('esp_lvgl_adapter','src/display/bridge/common/display_bridge_common.c'),('esp_lvgl_adapter','src/display/bridge/v9/lvgl_bridge_v9.c')]:tf.add(source/comp/rel,arcname=comp+'/'+rel)
 for f in sorted((o/'esp_lcd').rglob('*')):
  if f.is_file():tf.add(f,arcname='driver-overlay/esp_lcd/'+str(f.relative_to(o/'esp_lcd')))
 tf.add(o/'trace-manifest.json',arcname='driver-overlay/trace-manifest.json')
for name,root,files in [
 ('heavy-observer-rejected',Path('/tmp/present-s3-xip-on-integrity-20261110'),['controller.json','setup.json','visual-observations.json','driver-overlay/esp_lcd/src/esp_lcd_present_trace.c','driver-overlay/esp_lcd/include/esp_lcd_present_trace.h','driver-overlay/esp_lcd/rgb/esp_lcd_panel_rgb.c','s3-rgb/benchmark/present-xip-on-driver-trace/live.log','s3-rgb/benchmark/present-xip-on-driver-trace/manifest.json','s3-rgb/benchmark/present-xip-on-driver-trace/sdkconfig']),
 ('light-counter-control',Path('/tmp/present-s3-xip-on-light-20261110'),['controller.json','single-live.log','run_one.py','s3-rgb/benchmark/present-xip-on-driver-trace/manifest.json','s3-rgb/benchmark/present-xip-on-driver-trace/sdkconfig']),
 ('wrong-field-observer-rejected',r/'incomplete-attempt-wrong-source-field',['reason.txt','controller.json','manifest.json','sdkconfig','live.log'])]:
 for rel in files:
  f=root/rel
  if f.exists():
   dest=D/name/(rel+'.gz');dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(gzip.compress(f.read_bytes(),mtime=0))
(D/'host-validation.log').write_text('cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DPRESENT_TRACE_HOST test_address.c\nPASS; full test source archived.\n')

(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s3-rgb-xip-on-address-20261110');shutil.copytree(D,delivery,dirs_exist_ok=True)
print('ARCHIVED',D)
