from pathlib import Path
import json,re,csv,hashlib,gzip,shutil,subprocess,tarfile,io,html
C=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');A=Path('/tmp/present-s3-jitter-work-20261110');B=Path('/tmp/present-s3-jitter-chunked-work-20261110');T=Path('/tmp/present-s3-jitter-timing-work-20261110');L=Path('/tmp/present-s3-jitter-bounce40-work-20261110');D=C/'dev/evidence/s3-rgb-supply-20261110';customer=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s3-rgb-supply-20261110')
sets=[('original-present',A/'baseline-present',16,10,'off'),('original-adapter',A/'s3-rgb/benchmark/adapter-xip-off-driver-trace',16,10,'off'),('remove-duplicate-sync',A/'s3-rgb/benchmark/present-xip-off-driver-trace',16,10,'off'),('chunk4k-present',B/'s3-rgb/benchmark/present-xip-off-driver-trace',16,10,'off'),('chunk4k-adapter',B/'s3-rgb/benchmark/adapter-xip-off-driver-trace',16,10,'off'),('refill-timing',T/'baseline-timing-10line-16mhz',16,10,'off'),('bounce40',L/'s3-rgb/benchmark/present-xip-off-driver-trace',16,40,'off'),('pclk8',T/'s3-rgb/benchmark/present-xip-off-driver-trace',8,10,'off'),('xip-on',A/'s3-rgb/benchmark/present-xip-on-driver-trace',16,10,'on')]
rows=[];D.mkdir(parents=True,exist_ok=True)
for name,p,mhz,lines,xip in sets:
 t=(p/'live.log').read_text(errors='replace');assert 'SUITE_PASS ' in t,name;assert not any(s in t for s in ['ESP_ERROR_CHECK failed','Guru Meditation','assert failed']),name
 stats={};timings={}
 for line in t.splitlines():
  if line.startswith(('RGB_SUPPLY ','RGB_TIMING ')):
   kv=dict(re.findall(r'(\w+)=([^ ]+)',line));key=(kv.pop('mode'),int(kv.pop('rotation')));(stats if line.startswith('RGB_SUPPLY ') else timings)[key]={k:int(v) for k,v in kv.items()}
 passes=re.findall(r'PASS mode=(\S+).*?rotation=(\d+).*?lvgl_fps=(\d+).*?scenes=(\d+)',t)
 assert len(passes)==len(stats)
 for mode,rotation,fps,scenes in passes:
  assert int(scenes)==16;key=(mode,int(rotation));s=stats[key];ti=timings.get(key,{});assert s['short_frames']==s['restarts'];assert s['expected_eofs']==480//lines
  rows.append(dict(experiment=name,mode=mode,rotation=int(rotation),xip=xip,pclk_mhz=mhz,bounce_lines=lines,deadline_us=lines*520/mhz,frames=s['frames'],short_frames=s['short_frames'],restarts=s['restarts'],min_eofs=s['min_eofs'],expected_eofs=s['expected_eofs'],lvgl_fps=int(fps),scenes=16,max_refill_us=ti.get('max_refill_cycles',0)/240 if ti else None,max_memcpy_us=ti.get('max_memcpy_cycles',0)/240 if ti else None,slow_refills=next((v for k,v in ti.items() if k.startswith('slow_over')),None)))
 out=D/name;out.mkdir(exist_ok=True)
 for n in ['manifest.json','results.json','sdkconfig','bench_cases.h','live.log']:
  f=p/n
  if not f.exists():continue
  if n.endswith('.json'):shutil.copy2(f,out/n)
  else:(out/(n+'.gz')).write_bytes(gzip.compress(f.read_bytes(),mtime=0))
 assert len(re.findall(r'SCENE mode=',t))==16*len(passes)
assert len(rows)==14 and sum(x['scenes'] for x in rows)==224
# Public production code remains unchanged: no rejected candidate is shipped.
assert not subprocess.check_output(['git','diff','--','src','include'],cwd=C,text=True).strip()
IDF=Path('/home/xuxin/esp_work/esp-idf-qspi');assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=IDF,text=True).strip()
# XIP control changes only these three effective config values.
def config(p):return dict(l.split('=',1) for l in p.read_text().splitlines() if l.startswith('CONFIG_') and '=' in l)
x=config(A/'baseline-present/sdkconfig');y=config(A/'s3-rgb/benchmark/present-xip-on-driver-trace/sdkconfig');diff={k:[x.get(k,'n'),y.get(k,'n')] for k in x.keys()|y.keys() if x.get(k,'n')!=y.get(k,'n')};assert set(diff)=={'CONFIG_SPIRAM_XIP_FROM_PSRAM','CONFIG_SPIRAM_FETCH_INSTRUCTIONS','CONFIG_SPIRAM_RODATA'}
(D/'validation.json').write_text(json.dumps({'cases':14,'scenes':224,'original_runtime_unchanged':True,'base_idf_clean':True,'xip_sdk_differences':diff,'visual_acceptance':'not_passed','residual_xip_on_restarts':1,'original_firmware_restored':False},indent=2)+'\n')
(D/'summary.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
with (D/'summary.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
# Exact debug driver source snapshots; no firmware, dependency cache or build output.
for name,r in [('supply-observer',A),('timing-observer',T/'baseline-timing-10line-16mhz'),('bounce40-observer',L),('pclk8-observer',T)]:
 src=r/'driver-overlay';out=D/'driver-sources'/name;out.mkdir(parents=True,exist_ok=True);shutil.copy2(src/'trace-manifest.json',out/'trace-manifest.json')
 for rel in ['esp_lcd/rgb/esp_lcd_panel_rgb.c','esp_lcd/src/esp_lcd_present_trace.c','esp_lcd/include/esp_lcd_present_trace.h','esp_lcd/CMakeLists.txt']:
  dst=out/(rel+'.gz');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(gzip.compress((src/rel).read_bytes(),mtime=0))
for name,r in [('baseline',A/'baseline-present'),('rejected-no-duplicate',A/'candidate-no-duplicate')]:
 p=r/'present_lcd_fb_scanout.c';out=D/'candidate-sources'/name;out.mkdir(parents=True,exist_ok=True);shutil.copy2(p,out/p.name)
for r,n in [(A,'visual-now.json'),(B,'visual-observations.json')]:
 if (r/n).exists():shutil.copy2(r/n,D/('reported-'+n))
shutil.copy2(A/'hardware-policy.json',D/'hardware-policy.json');shutil.copy2('/tmp/setup_s3_jitter.py',D/'setup_s3_jitter.py');shutil.copy2('/tmp/archive_s3_supply.py',D/'archive_s3_supply.py')
for root,label in [(A,'baseline-and-xip'),(B,'chunk4k'),(T,'timing-and-pclk8'),(L,'bounce40')]:
 out=D/'controllers'/label;out.mkdir(parents=True,exist_ok=True)
 for p in root.glob('*controller.json'):shutil.copy2(p,out/p.name)
# Current exact application/bsp source is the final XIP-on 16MHz/10-line diagnostic.
shadow=Path('/tmp/present-s3-jitter-source-20261110/components/display/tools/esp_display_present');out=D/'final-test-source'
for rel in ['dev/test_apps/benchmark/main/main.c','dev/test_apps/boards/main/board_s3.c','dev/boards/configs/s3-rgb.defaults','dev/tools/run.py']:
 p=out/rel;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(shadow/rel,p)
shutil.copy2('/tmp/present-xip-matrix-20261110/pre-fix-source-snapshot.tar.gz',D/'pre-fix-source-snapshot.tar.gz');shutil.copy2('/tmp/present-xip-matrix-20261110/pre-fix-source-sha256.json',D/'pre-fix-source-sha256.json')
text='''# S3 RGB 供数时限诊断（2026-11-10）

用户明确反馈：S3 的 Multi RGB、Container 类场景严重撕裂，甚至全屏抖动。不是靠目视来推断结论：本轮读取驱动的真实 bounce EOF 数、已有 DMA 自动重启分支，并测量补数函数耗时。

原16MHz时序每行520个时钟，10行bounce只有325微秒的补数窗口。9.6KB的PSRAM memcpy最长682.87微秒，完整补数693.63微秒；200350次补数中11695次超过窗口。这是可以直接验证的供数超时，能造成驱动DMA自动重启及画面失步。耗时测量包含更高优先级中断可能造成的延迟，不能据此将全部时间都归入PSRAM芯片本体带宽；精确的总线争用来源尚未全部分离。

| 实验（FULL 0°） | 补数不足 / VSYNC | DMA重启 | 补数窗口 | 最长补数 |
|---|---:|---:|---:|---:|
| present XIP OFF 原配置 | 886 / 4207 | 886 | 325 us | 未计时 |
| adapter XIP OFF 原配置 | 738 / 4197 | 738 | 325 us | 未计时 |
| present 去掉重复RGB cache同步 | 914 / 4206 | 914 | 325 us | 未计时 |
| present IDF 4KiB分段cache同步 | 865 / 4211 | 865 | 325 us | 未计时 |
| present 原配置加只读计时 | 895 / 4212 | 895 | 325 us | 693.63 us |
| present 40行bounce、16MHz | 238 / 4181 | 238 | 1300 us | 2461.79 us |
| present 10行bounce、8MHz | 0 / 2103 | 0 | 650 us | 643.39 us |
| present 10行bounce、16MHz、XIP ON | 1 / 4200 | 1 | 325 us | 未计时 |

完整14项、224个场景的结果见[CSV](summary.csv)和[JSON](summary.json)，含PARTIAL对照。每项完整运行LVGL的16场景，每配置一次运行；这些不是多轮统计均值。

**结论与边界：**

- 两种实现都能在XIP OFF重负载下触发供数不足，不能认定问题只存在于present。
- 去掉额外cache同步、打开4KiB分段同步均未改善到可接受水平，不纳入生产修复。用户对CHUNK4K也明确反馈仍明显抖动。
- 增加bounce只降低发生率，没有根治；40行比10行多占56.25KiB内部堆，不将其当作最终方案。
- 仅减半PCLK消除了这一项供数不足，支持补数时限/负载关系的解释；它同时将屏幕刷新率从约60.33Hz降到30.17Hz，不能冒充原配置修复，且643微秒的最坏耗时距650微秒时限余量很小。
- 原16MHz下开启XIP显著改善，实际SDK仅XIP、FETCH_INSTRUCTIONS、RODATA三项变化，见validation.json。仍有1次重启，尚未定位发生阶段，不当作零异常或完整修复。取指路径与PSRAM/缓存争用是关键条件，但尚未证明单一底层仲裁事件的因果关系。
- RGB地址观察在赋值后采样，目标一致不证明所有写入与扫描交接都正确；本轮不能据此排除另有buffer生命周期问题。
- 14项只覆盖本页列出的模式/角度，不能外推为所有模式/旋转都正常。

这些是隔离诊断固件，带顶部身份标签及只读驱动计数，具有额外开销；FPS不混入原正式矩阵，也不覆盖旧成绩。生产组件src/include及基础IDF保持不变。驱动只记录，不增加assert、返回错误或修正选择。原先正式矩阵仍有P4 XIP OFF结束失败及缺项，未宣称全部完成。

按用户要求不烧回原固件。最后留在S3上的是原16MHz、10行bounce、XIP ON诊断固件；没有将8MHz诊断状态留作默认。原固件备份继续保留，但不进入本证据目录。本目录不含build、依赖缓存、固件或flash备份镜像。
'''
(D/'README.md').write_text(text)
htmlrows=''.join('<tr>'+''.join('<td>'+html.escape(str(row[k]))+'</td>' for k in ['experiment','mode','rotation','xip','pclk_mhz','bounce_lines','frames','short_frames','restarts','deadline_us','max_refill_us'])+'</tr>' for row in rows)
(D/'comparison.html').write_text('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S3 RGB补数诊断</title><style>body{font-family:system-ui;margin:24px;line-height:1.6}table{border-collapse:collapse}td,th{padding:8px;border:1px solid #ccc;white-space:nowrap}.wrap{overflow:auto}.notice{background:#fff0cd;padding:16px}</style><h1>S3 RGB：补数时限诊断</h1><p class="notice">供数超时已抓到；原16MHz开启XIP后仍有1次重启，不能宣称完全修复。正式矩阵另有P4失败缺项。</p><p>10行 / 16MHz窗口325μs；实测最长补数694μs。仅降低PCLK到8MHz后0/2103次不足；恢复16MHz、仅开启XIP后1/4200次不足。</p><p><a href="README.md">详细结论和边界</a> · <a href="summary.csv">CSV</a></p><div class="wrap"><table><tr>'+''.join('<th>'+x+'</th>' for x in ['实验','模式','角度','XIP','PCLK MHz','bounce行','VSYNC','不足','重启','时限μs','最长补数μs'])+'</tr>'+htmlrows+'</table></div>')
(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
shutil.copytree(D,customer,dirs_exist_ok=True);print('ARCHIVED',len(rows),'cases',D,customer)
