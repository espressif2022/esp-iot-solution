import csv,hashlib,json,re
from pathlib import Path
r=Path('/tmp/present-rgb-benchmark-20261105');s=json.loads((r/'summary.json').read_text());m=json.loads((r/'manifest.json').read_text())
text=(r/'s3-rgb-live.log').read_text(errors='replace');clean=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',text)
modes=['NONE','DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];angles=[0,90,180,270]
expected={(mode,angle) for mode in modes for angle in angles};observed={(x['mode'],x['rotation']) for x in s['pass']}
checks={'expected_case_count':28,'observed_case_count':len(s['pass']),'missing':sorted(expected-observed),'unexpected':sorted(observed-expected),'unique_cases':len(observed)==len(s['pass']),'all_16_scenes':all(x['scenes']==16 for x in s['pass']),'boot_banner_count':clean.count('ESP-ROM:'),'error_lines':[x for x in clean.splitlines() if re.match(r'E \(',x)],'warning_lines':sorted(set(x.split(') ',1)[-1] for x in clean.splitlines() if re.match(r'W \(',x)))}
checks['matrix_completed']=s['complete'];checks['no_runtime_failure']=not s['errors'] and not checks['error_lines'] and checks['boot_banner_count']==1
(r/'validation.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2))
if s['pass']:
 with (r/'results.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(s['pass'][0]));w.writeheader();w.writerows(s['pass'])
scenes=[]
pat=re.compile(r'SCENE mode=(\w+) te_buffers=(\d+) rotation=(\d+) name="([^"]+)" fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms')
for match in pat.finditer(clean):
 scenes.append(dict(zip(['mode','te_buffers','rotation','scene','fps','cpu_percent','render_ms','flush_ms'],[match[1],int(match[2]),int(match[3]),match[4],*map(int,match.group(5,6,7,8))])))
if scenes:
 with (r/'scenes.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(scenes[0]));w.writeheader();w.writerows(scenes)
lines=['# S3 RGB LVGL benchmark：scanout 修复后模式与角度矩阵（2026-11-05）','',
'设备：ESP32-S3 R16，GC9503 480×480 RGB，MAC 90:e5:b1:d0:a9:d8，USB0。',
'使用现有 examples/display/gui/lvgl_present_benchmark，完整 lv_demo_benchmark，每组 16 个场景，未缩短场景。RGB565，刷新周期 15ms，50 行单 tile，2 个 LVGL 软件绘制单元，RGB bounce 10 行，S3 FB_IRAM 开启，PSRAM 80MHz/XIP。实际驱动分配 3 个 FB，各模式使用其所需数量。',
'修复：提交成功后排空在途 ISR，成功后以一次后续 frame-complete 归还旧 FB；排空不可用/失败保守等两次。IDF esp_lcd 无修改。',
'','## LVGL 汇总 FPS（单轮结果）','','| 模式 | 0° | 90° | 180° | 270° |','|---|---:|---:|---:|---:|']
by={(x['mode'],x['rotation']):x for x in s['pass']}
for mode in modes:lines.append('| '+mode+' | '+' | '.join(str(by[(mode,a)]['lvgl_fps']) if (mode,a) in by else '待完成' for a in angles)+' |')
lines+=['','## 传输完成计数 / 测量时间（次/秒）','','| 模式 | 0° | 90° | 180° | 270° |','|---|---:|---:|---:|---:|']
for mode in modes:lines.append('| '+mode+' | '+' | '.join(str(by[(mode,a)]['transport_fps']) if (mode,a) in by else '待完成' for a in angles)+' |')
lines+=['','LVGL 汇总 FPS 是各场景统计的平均；transport_fps 是整个测量窗口内完成帧数除以耗时，两者不是同一指标，均非光学帧率。CPU 列为 LVGL 的 CPU 指标，不表示每核实测占用率。单轮结果不是统计性能保证。',
'',f"完成 {len(s['pass'])}/28 组；TE_SYNC 的单/双 compose × 四方向共 {len(s['skip'])} 组在 RGB 上跳过，不作为 RGB 有效测试。完整检查见 validation.json，分场景数据见 scenes.csv，原始串口见 s3-rgb-live.log。",'',
'## 目视与验证边界','',
'用户在红绿蓝场景报告过撕裂，报告时测试大致位于 NONE / 90° → 180° 阶段（无法对应精确帧）。NONE 明确允许写当前扫描缓冲，本身不保证无撕裂。原始观察见 optical-observations.json。双/三缓冲模式尚无逐项完整目视验收，不能从串口 PASS 推断光学无撕裂。',
'本轮未插入驱动地址诊断，也未做面板像素读回、FIFO/DMA 波形、restart 压力或逐帧退休地址验证。FULL/DIRECT 旋转和 DIRECT 的低吞吐仍需后续分析，不因矩阵完成而视为全部解决。',
'', '## 版本与恢复','',f"仓库 HEAD：{m['repo_head']}（包含工作区修复，实际源码与固件 SHA256 见 manifest.json）；IDF：{m['idf_head']}。",
'测试前备份完整 16MiB Flash，并用设备 digest 校验一致。原分区表与 benchmark 不同，测试烧录 bootloader、分区表及 app；恢复状态见 restore-status.json。原始 Flash 备份仅留在本地临时目录，不随证据上传或归档。',
'历史同工程结果 previous-project-run.txt 仅供参考，未作为本轮受控 A/B 的性能证明。']
(r/'README.md').write_text('\n'.join(lines)+'\n')
print(json.dumps({'passes':len(s['pass']),'complete':s['complete'],'scene_rows':len(scenes),'checks':checks},ensure_ascii=False))
