from pathlib import Path
import json,csv,hashlib,html,gzip,shutil
from decimal import Decimal,ROUND_HALF_UP
R=Path('/tmp/present-s3-iram-off-20261109');BASE=Path('/tmp/present-five-board-20261109/s3-rgb/benchmark');OFF=R/'s3-rgb/benchmark/present'
OUT=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s3-fb-iram-on-off-20261109')
state=json.loads((R/'stage.json').read_text());assert state['action']=='done' and state['tests_succeeded'] and state['restored']
proof=json.loads((R/'configuration-proof.json').read_text());assert proof['passed'] and proof['differences']=={'CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM':['y','n']}
raw={i:json.loads((p/'results.json').read_text()) for i,p in [('on',BASE/'present'),('off',OFF),('adapter',BASE/'adapter')]}
for i,r in raw.items():assert r['complete'] and r['matrix_complete'] and r['memory_complete'] and not r['errors'];assert all(x['scenes']==16 for x in r['rows'] if x['status']=='PASS')
assert len(raw['on']['rows'])==len(raw['off']['rows'])==24
index={i:{(x['mode'],x['rotation']):x for x in r['rows']} for i,r in raw.items()};assert index['on'].keys()==index['off'].keys()
phases=[('before_open','打开前剩余'),('after_open','打开后剩余'),('after_run','运行最小剩余'),('after_close','关闭后剩余')]
rows=[]
for key,on in index['on'].items():
 off=index['off'][key];a=index['adapter'].get(key,{})
 row=dict(mode=key[0],rotation=key[1],on_fps=on['lvgl_fps'],off_fps=off['lvgl_fps'],adapter_fps=a.get('lvgl_fps','-'),on_render_ms=on['render'],off_render_ms=off['render'],on_flush_ms=on['flush'],off_flush_ms=off['flush'])
 for heap in ['internal','psram']:
  for phase,_ in phases:
   for side,item in [('on',on),('off',off)]:row[f'{side}_{heap}_{phase}_free_bytes']=item['memory'][phase][heap+('_min_free' if phase=='after_run' else '_free')]
 rows.append(row)
on_init=raw['on']['suite_memory']['before_init'];off_init=raw['off']['suite_memory']['before_init']
meta=dict(production_commit='b607bb9a1326682c1eb3f3a980d983885338fd02',configuration_diff=proof['differences'],cases=24,scenes_per_case=16,board='S3 R16 RGB480x480',mac='90:e5:b1:d0:a9:d8',instruction_cache_kib=32,psram_xip=True,internal_heap_gain_bytes=off_init['internal_total']-on_init['internal_total'],psram_heap_gain_bytes=off_init['psram_total']-on_init['psram_total'],restored=json.loads((R/'s3-rgb/backup/restored.json').read_text()),on_is_earlier_today_baseline=True,single_run=True)
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'comparison.json').write_text(json.dumps(dict(metadata=meta,rows=rows),ensure_ascii=False,indent=2)+'\n');(OUT/'raw-results.json').write_text(json.dumps(raw,ensure_ascii=False,indent=2)+'\n')
with (OUT/'comparison.csv').open('w',encoding='utf-8-sig',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def esc(v):return html.escape(str(v))
def fixed(value,precision=2):
 return format(Decimal(str(value)).quantize(Decimal(1).scaleb(-precision),rounding=ROUND_HALF_UP),f'.{precision}f')
def delta(value,baseline,precision=0,unit=''):
 d=value-baseline;fmt=fixed(abs(d),precision);return f'<span class="delta {"down" if d<0 else "same" if d==0 else ""}" data-delta="{d}">{"⬆️ +" if d>0 else "⬇️ -" if d<0 else "＝ "}{fmt}{unit}</span>'
def fps(r,side):
 v=r[side+'_fps'];return str(v)+(delta(v,r['on_fps']) if side=='off' else '')
def memory(r,side,heap,phase):
 v=r[f'{side}_{heap}_{phase}_free_bytes'];return f'<span class="memory-value">{fixed(v/1024)}</span>'+(delta(v/1024,r[f'on_{heap}_{phase}_free_bytes']/1024,2) if side=='off' else '')
style='body{font:15px/1.6 system-ui;background:#f5f7fa;color:#172b45;margin:0}main{max-width:1250px;margin:auto;padding:28px}section{background:white;padding:20px;border:1px solid #dbe3ec;border-radius:12px;margin:20px 0}h1{font-size:26px}h2{font-size:20px}table{border-collapse:collapse;width:100%;font-size:14px}th,td{border-bottom:1px solid #dbe3ec;padding:9px 12px;text-align:right;white-space:nowrap}th:first-child,td:first-child{text-align:left}th{background:#f1f5f9}.off{background:#e7f5f0}.on{background:#eef1fa}.delta{color:#447064;font-size:12px;margin-left:6px}.down{color:#a74f41}.same,.muted{color:#64748b}.wrap{overflow:auto}a{color:#22629d}summary{cursor:pointer;font-weight:600}.scope{background:#e6eef9;padding:15px;border-radius:10px}'
s=[f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>S3 FB_IRAM 开关实测</title><style>{style}</style><main><h1>S3 RGB：FB_IRAM 开启 / 关闭实测</h1>', '<p>2026-11-09 · 480×480 · 32 KiB 指令缓存 · PSRAM XIP · 同版本生产代码与干净 IDF · 每项完整16场景</p>', '<div class="scope">ON 是今天已完成的开启矩阵；OFF 为本次重新上板的关闭矩阵。唯一 SDK 配置差异为 CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM，生产源码与测试输入指纹一致。<br>OFF 单元格的箭头只与 ON 比较：FPS／剩余内存越多越好。绿色上箭头表示增加，红色下箭头表示减少。adapter 是今天同板的参考成绩。单轮整数均值，小幅差值不代表统计显著变化。</div>',f'<p>关闭后内部堆总容量增加 <b>{meta["internal_heap_gain_bytes"]/1024:.2f} KiB</b>；PSRAM堆总容量变化 <b>{meta["psram_heap_gain_bytes"]/1024:+.2f} KiB</b>。组件默认仍为开启；本轮只修改隔离测试配置。</p>', '<p><a href="comparison.csv">原始字节 CSV</a> · <a href="comparison.json">JSON</a> · <a href="README.md">口径与恢复证据</a></p><section><h2>完整帧率对照</h2><div class="wrap"><table id="fps"><thead><tr><th>模式</th><th>角度</th><th>ON</th><th>OFF（相对ON）</th><th>adapter参考</th></tr></thead><tbody>']
for r in rows:s.append(f'<tr><td>{esc(r["mode"])}</td><td>{r["rotation"]}°</td><td class="on">{fps(r,"on")}</td><td class="off">{fps(r,"off")}</td><td>{esc(r["adapter_fps"])}</td></tr>')
s+=['</tbody></table></div></section><section><h2>四时点剩余内存 · KiB · 越多越好</h2><details><summary>展开全部模式、角度的RAM／PSRAM对照</summary><div class="wrap"><table id="memory"><thead><tr><th>模式 / 角度</th><th>时点</th><th>ON RAM</th><th>OFF RAM</th><th>ON PSRAM</th><th>OFF PSRAM</th></tr></thead><tbody>']
for r in rows:
 for j,(phase,label) in enumerate(phases):
  cells=''.join(f'<td class="{side}">{memory(r,side,heap,phase)}</td>' for heap in ['internal','psram'] for side in ['on','off'])
  s.append('<tr>'+(f'<td rowspan="4">{esc(r["mode"])} / {r["rotation"]}°</td>' if j==0 else '')+f'<td>{label}</td>{cells}</tr>')
s+=['</tbody></table></div></details></section><p class="muted">相同RGB硬件初始化，统一创建3个driver framebuffer；不是各模式独立最小配置。内存是整机对应时点剩余，运行最小是各heap低水位之和，不保证同时发生。两份都是present，首次LVGL初始化边界一致。测试后原固件已恢复并整片校验；串口完成不代表已进行光学/目视防撕裂验收。</p></main></html>']
(OUT/'comparison.html').write_text('\n'.join(s)+'\n')
lines=['# S3 FB_IRAM 当前版本开关对照（2026-11-09）','', '生产源码 `b607bb9a1`；同板MAC `90:e5:b1:d0:a9:d8`，480×480 RGB，80MHz PSRAM、32KiB指令缓存、XIP。IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 干净无补丁。LVGL 9.6.0~1。', '', 'ON来自今天五板测试的完整24项，OFF本轮重新烧录完整24项，每项完整16场景；未缩短场景。全SDK唯一差别为FB_IRAM y/n，生产文件与共享测试源码指纹相同。单轮结果，不是交错重复的统计检验。', '', f'内部堆总容量OFF−ON：{meta["internal_heap_gain_bytes"]} B（{meta["internal_heap_gain_bytes"]/1024:.2f}KiB）；PSRAM总容量差：{meta["psram_heap_gain_bytes"]}B。', '', '| 模式 | 角度 | 开启 | 关闭 | adapter参考 |','|---|---:|---:|---:|---:|']
lines += [f'| {r["mode"]} | {r["rotation"]}° | {r["on_fps"]} | {r["off_fps"]} | {r["adapter_fps"]} |' for r in rows]
lines+=['','RAM与PSRAM保留打开前、打开后、运行最小、关闭后四时点，原始字节见CSV/JSON；网页KiB，OFF单元格的差值相对ON，剩余更多为好。内存是统一3个driver FB的整机测试基线，不是组件独占占用或独立模式最小配置。两份均为present，首次初始化边界一致。','', '本次关闭只作用于隔离测试工程，组件默认未修改。开关关闭不移除源码显式IRAM_ATTR；完整map压缩件和段统计保留。当前结果不能直接替代2026-11-03的16KiB指令缓存合成吞吐数据。','', '原固件重新整片备份并独立校验后才写测试固件；测试完成恢复同一份16MiB并独立校验。Flash备份、固件和构建目录不入交付件。没有新增目视/光学防撕裂验收。']
(OUT/'README.md').write_text('\n'.join(lines)+'\n')
def copy(src,dst):
 dst=OUT/'evidence'/dst;dst.parent.mkdir(parents=True,exist_ok=True)
 if src.suffix in ['.log','.map']:
  with dst.with_suffix(dst.suffix+'.gz').open('wb') as f:
   with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as g:g.write(src.read_bytes())
 else:shutil.copy2(src,dst)
for side,path in [('on',BASE/'present'),('off',OFF),('adapter',BASE/'adapter')]:
 for name in ['manifest.json','results.json','sdkconfig','bench_cases.h','live.log']:copy(path/name,Path(side)/name)
 if side!='adapter':copy(path/'build/present_benchmark.map',Path(side)/'link.map')
for name in ['scope.json','configuration-proof.json','on-map-summary.json','off-map-summary.json','events.jsonl','stage.json','controller.log']:copy(R/name,Path('run')/name)
for name in ['manifest.json','restored.json','restore.log','restore-verify.log']:copy(R/'s3-rgb/backup'/name,Path('restore')/name)
copy(Path('/tmp/run_present_s3_iram_off_20261109.py'),Path('scripts/run.py'));copy(Path(__file__),Path('scripts/report.py'))
(OUT/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(OUT)}\n' for p in sorted(OUT.rglob('*')) if p.is_file() and p.name!='SHA256SUMS'))
print(json.dumps(meta,indent=2))
