from pathlib import Path
import json,re,sys,csv,gzip,hashlib,shutil,html,importlib.util,datetime
from zoneinfo import ZoneInfo
R=Path('/tmp/present-s3-xip-off-all-modes-20261110');C=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present');S=Path('/tmp/present-s3-off-matrix-source-20261110/components/display/tools/esp_display_present');M=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];spec=importlib.util.spec_from_file_location('runner_single',S/'dev/tools/run.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
cohorts=[('present-existing','present',R/'s3-rgb/benchmark/present-xip-off-driver-trace',True),('adapter-existing','adapter',R/'adapter-prior-angle-data',True),('adapter-new','adapter',R/'s3-rgb/benchmark/adapter-xip-off-driver-trace',False)];look={};sources={};errors=[]
for name,impl,p,reused in cohorts:
 text=(p/'live.log').read_text(errors='replace') if (p/'live.log').exists() else '';parsed=runner.parse_log(text);stats={}
 for line in text.splitlines():
  if line.startswith('RGB_SUPPLY '):
   d=dict(re.findall(r'(\w+)=([^ ]+)',line));mode=d.pop('mode');rotation=int(d.pop('rotation'))
   if rotation==0:stats[mode]={k:int(v) for k,v in d.items()}
 sources[name]={'path':str(p),'whole_sequence_complete':parsed['complete'],'reused':reused}
 for x in parsed['rows']:
  if x['rotation']!=0:continue
  key=impl,x['mode'];assert key not in look,(key,'duplicate selected mode')
  if x['status']=='SKIP':look[key]={'implementation':impl,'mode':x['mode'],'angle':0,'status':'SKIP','verdict':'不支持','source':name};continue
  assert x['scenes']==16 and len(re.findall(r'SCENE mode='+re.escape(x['mode'])+r' rotation=0 name=',text))==16
  audit=x['buffer_audit'];assert audit['elapsed_us']>=60000000 and audit['input_tiles']==1
  s=stats[x['mode']];assert s['expected_eofs']==48 and s['measured_frames']>=3000 and s['measured_short']<=s['short_frames'] and s['measured_restarts']<=s['restarts']
  look[key]={'implementation':impl,'mode':x['mode'],'angle':0,'status':'PASS','verdict':'运行期触发供数异常' if s['measured_short'] or s['measured_restarts'] else '本轮运行期未触发','source':name,'reused':reused,'scenes':16,'measured_frames':s['measured_frames'],'measured_short':s['measured_short'],'measured_restarts':s['measured_restarts'],'window_outside_short':s['short_frames']-s['measured_short'],'window_outside_restarts':s['restarts']-s['measured_restarts'],'total_short':s['short_frames'],'total_restarts':s['restarts']}
rows=[look.get((i,m),{'implementation':i,'mode':m,'angle':0,'status':'pending','verdict':'未完成'}) for i in ['present','adapter'] for m in M];passed=sum(x['status']=='PASS' for x in rows);skipped=sum(x['status']=='SKIP' for x in rows);bad=[x for x in rows if x['verdict']=='运行期触发供数异常'];complete=passed==11 and skipped==1
state=json.loads((R/'single-angle-controller.json').read_text());summaries={i:{'tested':sum(x['implementation']==i and x['status']=='PASS' for x in rows),'affected':sum(x['implementation']==i for x in bad),'affected_modes':[x['mode'] for x in bad if x['implementation']==i]} for i in ['present','adapter']};data={'complete':complete,'passed':passed,'skipped':skipped,'selected_scenes':passed*16,'affected':len(bad),'angle':0,'conditions':json.loads((R/'single-angle-setup.json').read_text()),'rows':rows,'sources':sources,'summaries':summaries,'controller':state,'updated':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()};(R/'single-angle-data.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
fields=['implementation','mode','angle','status','verdict','source','reused','scenes','measured_frames','measured_short','measured_restarts','window_outside_short','window_outside_restarts','total_short','total_restarts']
with (R/'single-angle.csv').open('w') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
E=html.escape;body='<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S3 XIP OFF单角度模式验证</title><style>body{font-family:system-ui;margin:24px;line-height:1.7;color:#203040}main{max-width:1050px;margin:auto}table{border-collapse:collapse;width:100%}td,th{border:1px solid #ccc;padding:10px;text-align:right}th:first-child{text-align:left}.bad{color:#a13120;background:#fff0ec}.wait{color:#94710e}.notice{background:#fff1cb;padding:14px}small{display:block;color:#607080}.wrap{overflow:auto}</style><main><h1>S3 RGB · XIP OFF · 0°模式验证</h1><p class="notice">'+('采集完成' if complete else '补测中')+f'：{passed}/11项支持组合、{skipped}/1项不支持；{len(bad)}项在运行期间抓到供数不足或DMA重启。</p><p>固定16MHz、十行bounce（9600B×2）、单输入tile、原始提交逻辑。按用户要求仅用0°判断是否可触发；已完成的数据复用，只补缺失的四种adapter模式。</p><div class="wrap"><table><tr><th>模式（0°）</th><th>present：运行期不足 / 扫描帧</th><th>adapter：运行期不足 / 扫描帧</th></tr>'
for m in M:
 body+='<tr><th>'+m+'</th>'
 for i in ['present','adapter']:
  x=next(x for x in rows if x['implementation']==i and x['mode']==m)
  if x['status']=='SKIP':body+='<td>−</td>'
  elif x['status']=='pending':body+='<td class="wait">未完成</td>'
  else:body+='<td class="'+('bad' if x in bad else '')+'">'+str(x['measured_short'])+' / '+str(x['measured_frames'])+'<small>运行期重启 '+str(x['measured_restarts'])+'；窗口外不足 '+str(x['window_outside_short'])+'</small></td>'
 body+='</tr>'
body+='</table></div><p>每项完整跑16场景；PASS仅说明benchmark结束，异常看驱动计数。运行期从RUN之后下一VSYNC起，排除跨阶段的首半帧，结束RUN后停止，含benchmark结束回调、不含随后close。零表示本轮未触发，不保证任何负载下都正常。NONE按要求排除；adapter AUTO不支持。</p><p>present六项0°来自已完整结束的既有序列；adapter DOUBLE_FULL 0°也已完整结束，随后因用户缩减范围中止其后角度。复用的是已完成的单项16场景，并未将被中断的场景补成成绩。其余四项是本次单角度补测。数据源和原始序列是否完整在JSON中明示。诊断有标签和只读计数开销，FPS不替换正式成绩。</p><p><a href="comparison.csv">CSV</a> · <a href="matrix-data.json">JSON</a></p></main>'
(R/'single-angle-comparison.html').write_text(body)
if '--archive' in sys.argv:
 assert state['stage']=='done' and state['success'] and complete
 D=C/'dev/evidence/s3-rgb-xip-off-mode-check-20261110';D.mkdir(parents=True,exist_ok=True)
 def cp(p,d):d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,d)
 def gz(p,d):d.parent.mkdir(parents=True,exist_ok=True);d.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 for src,dst in [('single-angle-comparison.html','comparison.html'),('single-angle-data.json','matrix-data.json'),('single-angle.csv','comparison.csv'),('single-angle-controller.json','controller.json'),('single-angle-setup.json','setup.json'),('hardware-policy.json','hardware-policy.json'),('builds-ready.json','builds-ready.json'),('test_measurement.c','test_measurement.c'),('measurement-host-test.log','measurement-host-test.log'),('report_single_angle.py','report_single_angle.py'),('run_single_angle.py','run_single_angle.py')]:cp(R/src,D/dst)
 for name,impl,p,reused in cohorts:
  for n in ['manifest.json','results.json']:
   if (p/n).exists():cp(p/n,D/name/n)
  for n in ['live.log','sdkconfig','bench_cases.h','build.log']:
   if (p/n).exists():gz(p/n,D/name/(n+'.gz'))
 for n in ['visual-observations.json','controller-before-reduction.json']:
  if (R/n).exists():cp(R/n,D/n)
 for n in ['single-flash.log','single-capture.log','single-report.log','single-reference-flash.log']:
  if (R/n).exists():gz(R/n,D/'operations'/(n+'.gz'))
 o=R/'driver-overlay';cp(o/'trace-manifest.json',D/'driver-observer/trace-manifest.json')
 for rel in ['esp_lcd/rgb/esp_lcd_panel_rgb.c','esp_lcd/src/esp_lcd_present_trace.c','esp_lcd/include/esp_lcd_present_trace.h']:gz(o/rel,D/'driver-observer'/(rel+'.gz'))
 for rel in ['dev/test_apps/benchmark/main/main.c','dev/test_apps/boards/main/board_s3.c','dev/boards/configs/s3-rgb.defaults','src/platform/lcd/present_lcd_fb_scanout.c']:cp(S/rel,D/'test-source'/rel)
 lines=['# S3 RGB XIP OFF 单角度模式判定（2026-11-10）','','[判定表](comparison.html) · [CSV](comparison.csv) · [逐项数据与来源](matrix-data.json)','','按用户最新要求仅0°，复用已有完整结果，只补缺失模式。11个支持组合，每项完整16场景，共176个选定场景；adapter AUTO记−，NONE不纳入。原较大序列中额外角度原始记录不删除，但不增加本页验证范围，也没有继续补跑其他角度。','']
 for i,s in summaries.items():lines.append(f'- {i}：{s["tested"]}个模式已验证，{s["affected"]}个模式运行期触发；'+', '.join(s['affected_modes'])+'。')
 lines+=['','固定原16MHz PCLK、十行bounce、240MHz CPU、80MHz PSRAM、XIP OFF、单输入tile。只读驱动观察补数EOF不足和既有自动重启，不修改控制逻辑。运行期门控排除跨入创建阶段的首半帧，窗口包含benchmark结束回调；创建/关闭等窗口外异常另外列。Host ASan/UBSan验证门控、首半帧、重置和错误panel过滤。','', 'present六项0°来自已完整结束的既有序列。adapter DOUBLE_FULL 0°也已完整结束，其后的其他角度因用户缩减范围被中止；旧序列未整体跑完在来源元数据中如实标注，复用的是已完整关闭并PASS的那一项16场景。其余四模式单角度补测正常结束。没有将被中断的场景当成有效成绩。','', '结论仅限这块S3 R16/GC9503及该配置的benchmark负载，不外推所有S3硬件或所有负载。零计数只能说本轮未触发。FPS受诊断开销影响，不替换旧正式性能表。生产组件和基础IDF不改；补数时限325us、此前最长实测694us的归因详见相邻s3-rgb-supply-20261110。','', '按用户要求不恢复原固件。结束后保留已测XIP ON/原16MHz诊断参考固件，状态见controller.json。归档不含build、依赖缓存、固件或flash备份。']
 (D/'README.md').write_text('\n'.join(lines)+'\n');(D/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(D))+'\n' for p in sorted(D.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'));delivery=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s3-rgb-xip-off-mode-check-20261110');shutil.copytree(D,delivery,dirs_exist_ok=True);print('ARCHIVED',D)
print(json.dumps({'complete':complete,'supported':passed,'unsupported':skipped,'affected':len(bad),'summaries':summaries},ensure_ascii=False))
