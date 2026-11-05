import csv,json,re
from pathlib import Path
r=Path('/tmp/present-direct-fix-board-20261105');s=json.loads((r/'summary.json').read_text());rows=list(csv.DictReader((r/'comparison.csv').open()))
text=re.sub(r'\x1b\[[0-9;]*[A-Za-z]','',(r/'s3-rgb-live.log').read_text(errors='replace'))
checks={'complete':s['complete'],'four_angles':sorted(x['rotation'] for x in s['pass'])==[0,90,180,270],'all_16_scenes':all(x['scenes']==16 for x in s['pass']),'error_lines':[x for x in text.splitlines() if x.startswith('E (')],'boot_count':text.count('ESP-ROM:'),'captured_failures':s['errors']}
assert checks['complete'] and checks['four_angles'] and checks['all_16_scenes'] and not checks['error_lines'] and checks['boot_count']==1
(r/'validation.json').write_text(json.dumps(checks,indent=2))
scenes=[]
for m in re.finditer(r'SCENE mode=(\w+) te_buffers=(\d+) rotation=(\d+) name="([^"]+)" fps=(\d+) cpu=(\d+)% render=(\d+)ms flush=(\d+)ms',text):
 scenes.append(dict(zip(['mode','rotation','scene','fps','cpu_percent','render_ms','flush_ms'],[m[1],int(m[3]),m[4],*map(int,m.group(5,6,7,8))])))
with (r/'scenes.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(scenes[0]));w.writeheader();w.writerows(scenes)
lines=['# DIRECT 已解析像素路径修复与 S3 RGB 复测（2026-11-05）','',
'问题：LVGL 已持有完整逻辑画布，flush 输出已算好的像素；backend 却为 DIRECT 先继承整屏旧图，旋转时还整屏回拷/转换。此前原始 adapter 对照发现 DIRECT 约一半帧率退化。',
'', '## 修改','',
'- 内部 frame request 新增 resolved_pixels：生产者不读取目标 lease 旧像素，实际提交区域仍需完整定义最终像素。LVGL copy DIRECT 自动使用此契约；单 FB active-write/NONE 不启用。公开应用 API 不变。',
'- 对此路径跳过可读 prefill。非旋转直接写候选物理 FB；旋转/字节序转换只处理实际提交区域，异步转换完成前不归还逻辑暂存。',
'- 损伤、版本及 repair 使用物理坐标，只从不可变物理 baseline 修补缺失的历史损伤，避开已写入的新像素；未知历史、首用和取消后仍保守补全。',
'- 普通 read-modify-write DIRECT 继续继承旧像素；切换契约时清空历史版本并保留硬件引用，必要时全范围修补。扫描退休协议、原始 adapter、IDF 均未修改。',
'', '## 同板完整 LVGL benchmark：DIRECT','',
'ESP32-S3 R16 / GC9503 480×480 RGB，MAC 90:e5:b1:d0:a9:d8。同 IDF、LVGL 9.6.0~1，相同 benchmark 源码、CPU/PSRAM/缓存/编译/LVGL 配置；RGB565、15ms、50 行单 tile、2 个绘制单元、10 行 bounce。使用隔离的同工程副本，仅将运行矩阵缩到 DIRECT 的四个角度，每个角度完整 16 场景，没有缩短场景。',
'', '| 角度 | 原始 adapter FPS | 修复前 present FPS | 本次 present FPS |','|---|---:|---:|---:|']
for x in rows:lines.append(f"| {x['rotation']}° | {x['original_adapter_fps']} | {x['before_fix_fps']} | {x['after_fix_fps']} |")
lines+=['', '四组、64 个场景均完成；串口没有断言、超时、额外重启或错误级日志。0° 接近原始 adapter，旋转本轮超过原始 adapter。FPS 是 LVGL 场景汇总，不是光学 FPS；均为单轮结果，非多轮统计。比较证明该修复能显著改善退化，未量化每一笔复制/调度的独立贡献。',
'', '## 回归','',
'- LVGL 9.5 与 9.6 的 host ASan/UBSan suite 均 37/37 通过（detect_leaks=0，未验证泄漏检测）。9.6 host 配置使用新的 RGB565 格式宏，避免废弃色深宏的 -Werror。',
'- 新 direct_resolved suite：7×5 非正方形，四方向 × 双/三缓冲 × 同步/异步 × RGB565/native、RGB565/swap、RGB888。逐像素核对 26 帧局部图像，覆盖取消、读改写/已解析像素契约切换；稳定小脏区断言总拷贝像素小于一整屏。',
'- 实际 LVGL 集成扩展到 7×9 四方向、RGB565/888、同步/异步，核对稀疏脏区与旧扫描缓冲不可写。',
'- 新增转换失败、面板拒收重试，确认失败图像不提交、排空后可释放。既有全部模式、像素执行器、扫描与停止回归通过。',
'- S3 实际 benchmark 编译/运行通过；P4 实际 smoke 编译通过，本轮未重新跑 P4 实板。',
'', '## 范围与恢复','',
'NONE 的单缓冲额外等待没有处理，先前约 25 FPS 的问题仍单独保留。未重新跑 FULL/PARTIAL 实板全矩阵，host 保留其回归。旧像素可读的 DIRECT 仍有继承成本，只有承诺提交最终像素的路径省去预填。',
'本轮没有完整目视/光学验收，不能以 benchmark PASS 证明无撕裂。测试前完整备份摘要再次核验一致，测试后恢复本次改写范围并独立核验完整 16MiB；最终状态见 restore-status.json。原始 Flash 备份仅留本地，不归档。',
'日志、comparison.csv、scenes.csv、源码/固件指纹及测试快照同目录保存。上两轮对照保留在同级 rgb-scanout-benchmark-20261105 与 rgb-adapter-compare-20261105。']
(r/'README.md').write_text('\n'.join(lines)+'\n');print('validated cases',len(s['pass']),'scene rows',len(scenes))
