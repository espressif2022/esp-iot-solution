import csv,json,re
from pathlib import Path
r=Path('/tmp/present-rgb-adapter-compare-20261105');a=json.loads((r/'summary.json').read_text());p=json.loads(Path('/tmp/present-rgb-benchmark-20261105/summary.json').read_text())
modes=['NONE','DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL'];angles=[0,90,180,270]
A={(x['mode'],x['rotation']):x for x in a['pass']};P={(x['mode'],x['rotation']):x for x in p['pass']}
skipped=set()
for line in a['skip']:
 m=re.search(r'SKIP mode=(\w+).*rotation=(\d+)',line)
 if m:skipped.add((m[1],int(m[2])))
rows=[]
for mode in modes:
 for angle in angles:
  key=(mode,angle);baseline=A.get(key);current=P[key]
  rows.append({'mode':mode,'rotation':angle,'adapter_status':'PASS' if baseline else 'SKIP' if key in skipped else 'PENDING','adapter_lvgl_fps':baseline['lvgl_fps'] if baseline else None,'present_lvgl_fps':current['lvgl_fps'],'difference_fps':current['lvgl_fps']-baseline['lvgl_fps'] if baseline else None,'difference_percent':round(100*(current['lvgl_fps']/baseline['lvgl_fps']-1),1) if baseline and baseline['lvgl_fps'] else None,'adapter_render_ms':baseline['render_ms'] if baseline else None,'present_render_ms':current['render_ms'],'adapter_flush_ms':baseline['flush_ms'] if baseline else None,'present_flush_ms':current['flush_ms']})
with (r/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
text=(r/'s3-rgb-live.log').read_text(errors='replace');clean=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',text)
errors=[x for x in clean.splitlines() if re.match(r'E \(',x)]
expected_errors=[x for x in errors if 'rotation not supported under TEAR_AVOID_MODE_NONE' in x]
unexpected_errors=[x for x in errors if x not in expected_errors]
checks={'matrix_completed':a['complete'],'passed':len(A),'skipped':len(skipped),'missing':sorted({(x,y) for x in modes for y in angles}-(set(A)|skipped)),'all_16_scenes':all(x['scenes']==16 for x in A.values()),'boot_banner_count':clean.count('ESP-ROM:'),'expected_unsupported_rotation_errors':expected_errors,'other_error_lines':unexpected_errors,'captured_runtime_failures':a['errors'],'adapter_transport_metric_available':False}
(r/'validation.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2))
scenes=[]
for m in re.finditer(r'SCENE mode=(\w+) te_buffers=(\d+) rotation=(\d+) name="([^"]+)" fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms',clean):
 scenes.append(dict(zip(['mode','te_buffers','rotation','scene','fps','cpu_percent','render_ms','flush_ms'],[m[1],int(m[2]),int(m[3]),m[4],*map(int,m.group(5,6,7,8))])))
if scenes:
 with (r/'adapter-scenes.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(scenes[0]));w.writeheader();w.writerows(scenes)
lines=['# S3 RGB：原始 esp_lvgl_adapter 与修复后 present 的 LVGL benchmark 对照（2026-11-05）','',
'同一块 ESP32-S3 R16 / GC9503 480×480 RGB，MAC 90:e5:b1:d0:a9:d8。present 先测，恢复原镜像后再测原始 adapter；不是交替顺序或多轮统计 A/B。',
'原始 adapter 为仓库中未修改的 0.7.4（无 HPx80 同步补丁）；present 为本轮 scanout 修复版本。两边使用相同 IDF、LVGL 9.6.0 及逐字节相同的 lv_demo_benchmark.c，CPU/PSRAM/缓存/LVGL/编译优化配置逐项匹配。RGB565，15ms 刷新周期，50 行单 tile，2 个 LVGL 绘制单元，RGB bounce 10 行，实际 panel 都分配 3 个 FB。应用分别为 lvgl_common_demo 和 lvgl_present_benchmark，集成任务/缓冲流程是被比较的实现差异。',
'', '## LVGL 汇总 FPS：原始 adapter → present','','| 模式 | 0° | 90° | 180° | 270° |','|---|---:|---:|---:|---:|']
for mode in modes:
 cells=[]
 for angle in angles:
  k=(mode,angle);label=str(A[k]['lvgl_fps']) if k in A else '不支持' if k in skipped else '待完成';cells.append(label+' → '+str(P[k]['lvgl_fps']))
 lines.append('| '+mode+' | '+' | '.join(cells)+' |')
lines+=['','present 的 AUTO 另测为 48 / 45 / 48 / 45 FPS，原始 adapter 没有直接对应的 AUTO 模式，未强行配对。',
'',f"原始 adapter：有效完成 {len(A)} 组、跳过 {len(skipped)} 组，矩阵完成标记 {a['complete']}；每组完整 16 场景。present 28/28 组和 448 个场景均完成。",'',
'## 如何解读','',
'LVGL FPS 是各有效场景统计的平均，单轮的小幅差异不能作为稳定性能增益保证。原始 adapter 日志的 frames=0 / transport_fps=0.0 是未实现计数的占位值，不是零吞吐；本报告不比较该列。render/flush 的归因随两种集成方式变化，不能仅凭某一列单独判断像素执行耗时。',
'NONE / 0° 存在明显差距：原始 adapter 51 FPS，present 25 FPS。源码上 present 单 FB active-write 仍经过 scanout exposure 完成门控，默认等待两次事件；原始 NONE 使用不同的刷新完成路径。这个时序差异值得单独做受控实验，本轮未修改 NONE 策略来追平数据。',
'DIRECT 的四个角度存在明显退化：原始 42 / 26 / 29 / 24，present 21 / 13 / 13 / 13 FPS，约下降 46%–55%。FULL 基本持平或本轮略高，PARTIAL 尚未出现这种退化。未支持的组合仅记 SKIP，不赋零分，也不修改原始 adapter 以使其通过。',
'从代码看，present 的 DIRECT 在继承不同 baseline 时可能全帧 prefill；旋转时还会全帧转换。LVGL 接入传递的损伤信息和上一帧继承策略应优先检查，实际各段贡献尚未通过插桩量化，本轮不把候选原因写成已证实根因。',
'', '## 画面与安全边界','',
'本轮没有逐项完整目视或光学采样，原始 adapter 的性能不证明其缓冲同步安全。此前用户在 present 的 NONE 90°→180° 附近报告过红绿蓝场景撕裂，NONE 允许写扫描缓冲，记录保留在上一轮 optical-observations.json。双/三缓冲是否彻底无撕裂，仍需独立目视和退休地址/时序验证。',
'原始 adapter 没有本次 post-submit ISR join 修复，不能为了 FPS 将已确认的安全要求退回原始协议。',
'', '## 证据与设备恢复','',
'comparison.csv 保存每个模式/角度的 FPS 差值和 render/flush；adapter-scenes.csv 保存原始 adapter 分场景数据；s3-rgb-live.log 为本轮原始串口；manifest.json 保存源码/配置/固件指纹。原始 adapter 仅由不支持的 NONE 旋转产生预期错误日志，其余异常单独列在 validation.json。',
'对照测试前再次验证整片 16MiB 与上一轮原始备份一致。测试结束后恢复本次改写范围，并独立核验完整 Flash，最终状态见 restore-status.json。备份含设备内容，只留本地，不随报告归档。',
'present 的原始测试详见同级 rgb-scanout-benchmark-20261105 证据目录。']
(r/'README.md').write_text('\n'.join(lines)+'\n')
print(json.dumps(checks,ensure_ascii=False))
