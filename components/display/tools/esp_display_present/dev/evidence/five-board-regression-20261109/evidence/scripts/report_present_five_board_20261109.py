from pathlib import Path
from decimal import Decimal,ROUND_HALF_UP
import json,csv,hashlib,re,html,zipfile,xml.etree.ElementTree as ET
ROOT=Path('/tmp/present-five-board-20261109')
OUT=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-adapter-five-board-20261109')
ROOTS={b:ROOT for b in ['s3-rgb','s31-rgb','p4-dsi','box3-spi']};ROOTS['s31-qspi']=ROOT/'te-retry';ROOTS['s31-rgb']=ROOT/'rgb-retry'
ORDER=['s3-rgb','s31-rgb','p4-dsi','s31-qspi','box3-spi']
LABELS={'s3-rgb':'S3 RGB · 480×480 · R16','s31-rgb':'S31 Korvo RGB · 800×480','p4-dsi':'P4 MIPI DSI · 1024×600','s31-qspi':'S31 mosaico QSPI · 480×480 · TE','box3-spi':'ESP-BOX-3 SPI · 320×240 · GRAM'}
PHASES=[('before_open','打开前剩余'),('after_open','打开后剩余'),('after_run','运行最小剩余'),('after_close','关闭后剩余')]
revision=json.loads((ROOT/'revision.json').read_text())
rows=[];summaries={};raw={};manifests={}
for b in ORDER:
 base=ROOTS[b];r={};m={}
 for impl in ['present','adapter']:
  path=base/b/'benchmark'/impl
  r[impl]=json.loads((path/'results.json').read_text());m[impl]=json.loads((path/'manifest.json').read_text())
  x=r[impl];assert x['complete'] and x['matrix_complete'] and x['memory_complete'] and not x['errors'],(b,impl)
  assert len(x['rows'])==len(m[impl]['cases'])
  assert all(t['scenes']==16 for t in x['rows'] if t['status']=='PASS')
  assert {(t['name'],t['rotation']) for t in m[impl]['cases']}=={(t['mode'],t['rotation']) for t in x['rows']}
  if b in ['s3-rgb','s31-rgb','p4-dsi']:assert all(t['mode']!='NONE' for t in x['rows'])
  if b=='s31-qspi':assert all(t['mode'].startswith(('TE_','ADAPTER_TE_')) for t in x['rows'])
 for key in ['board','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','test_sources_sha256','component_sources_sha256']:
  assert m['present'][key]==m['adapter'][key],(b,key)
 if b!='s31-qspi':assert m['present']['cases']==m['adapter']['cases']
 restore=json.loads((base/b/'backup/restored.json').read_text());backup=json.loads((base/b/'backup/manifest.json').read_text())
 assert restore['verified_full_flash'] and restore['sha256']==backup['sha256'] and backup['mac']==revision['boards'][b]['mac']
 summaries[b]={impl:{'pass':sum(x['status']=='PASS' for x in r[impl]['rows']),'skip':sum(x['status']=='SKIP' for x in r[impl]['rows'])} for impl in r}
 summaries[b].update(mac=backup['mac'],restored=True,original_sha256=backup['sha256'])
 raw[b]=r;manifests[b]=m
 lookup={impl:{(x['mode'],x['rotation']):x for x in r[impl]['rows']} for impl in r}
 keys=list(lookup['present'])+[k for k in lookup['adapter'] if k not in lookup['present']]
 for mode,angle in keys:
  row={'board':b,'mode':mode,'rotation':angle}
  for impl in ['present','adapter']:
   item=lookup[impl].get((mode,angle));row[impl+'_status']=item['status'] if item else 'NO_EQUIVALENT'
   row[impl+'_note']=item.get('reason','') if item else '配置结构不同，未强行映射同名模式'
   for key in ['lvgl_fps','cpu','render','flush','scenes','frames','wait_avg_us','tx_avg_us']:row[impl+'_'+key]=item.get(key,'-') if item else '-'
   for heap in ['internal','psram']:
    for phase,_ in PHASES:
     field=heap+('_min_free' if phase=='after_run' else '_free')
     row[f'{impl}_{heap}_{phase}_free_bytes']=item['memory'][phase][field] if item and item['status']=='PASS' else '-'
  rows.append(row)
assert len(rows)==120 and sum(isinstance(r['present_lvgl_fps'],int) for r in rows)==108 and sum(isinstance(r['adapter_lvgl_fps'],int) for r in rows)==73
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'comparison-current.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
(OUT/'raw-results.json').write_text(json.dumps(raw,ensure_ascii=False,indent=2)+'\n')
(OUT/'manifests.json').write_text(json.dumps(manifests,ensure_ascii=False,indent=2)+'\n')
with (OUT/'comparison-current.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
meta={'date':'2026-11-09','revision':revision,'boards':summaries,'row_count':len(rows),'present_grades':108,'adapter_grades':73,'all_restored_verified':True}
(OUT/'validation.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
TEMPLATE=r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>present / adapter 五板实测 · 2026-11-09</title>
<style>:root{color-scheme:light;--bg:#f5f7fa;--ink:#172b45;--muted:#64748b;--line:#dbe3ec;--present:#e7f5f0;--adapter:#eef1fa}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.6 system-ui,sans-serif}main{max-width:1560px;margin:auto;padding:28px}h1{font-size:28px;margin:0 0 10px}h2{font-size:21px;margin:0 0 6px}p{margin:8px 0}a{color:#22629d}.muted{color:var(--muted)}.scope{padding:15px 18px;background:#e6eef9;border-radius:12px;margin:18px 0}.nav{display:flex;gap:10px;flex-wrap:wrap;margin:18px 0}.nav a{padding:7px 12px;border:1px solid var(--line);background:white;border-radius:8px;text-decoration:none}.board{background:white;border:1px solid var(--line);border-radius:12px;padding:20px;margin:22px 0}.wrap{overflow-x:auto}table{border-collapse:collapse;width:100%;margin:12px 0;font-size:14px}th,td{padding:9px 12px;text-align:right;border-bottom:1px solid var(--line);white-space:nowrap}th{background:#f3f6fa}th:first-child,td:first-child{text-align:left}td.p{background:var(--present)}td.a{background:var(--adapter)}.delta{font-size:12px;color:#447064;margin-left:5px}.down{color:#a74f41}.same{color:var(--muted)}.note{white-space:normal;text-align:left;min-width:210px;font-size:13px}.phase{text-align:left}summary{cursor:pointer;font-weight:600;margin:16px 0}footer{color:var(--muted);margin:24px 0}.bad{color:#a74f41}code{background:#e7ecf2;padding:2px 5px;border-radius:4px}@media(max-width:800px){main{padding:12px}.board{padding:12px}h1{font-size:22px}th,td{padding:8px}}</style>
<main><h1>present / adapter 五板综合实测</h1><p class="muted">2026-11-09 · 固定源码 b607bb9a1 · LVGL 9.6.0~1 · 干净 IDF 7b9cc1ac79f8 · 每个有效组合完整16场景</p>
<div class="scope"><b>本轮范围</b><p>RGB / MIPI DSI 只列防撕裂模式，NONE 不进入矩阵。mosaico 只测 TE；允许撕裂的 GRAM 配置集中在 ESP-BOX-3。present 与 adapter 并排显示，present 单元格内标注相对 adapter 的 FPS 差值。内存同样标注 present − adapter 的剩余量差值：绿色上箭头表示剩余更多，红色下箭头表示更少；无等价项不计算。</p><p>全部成绩来自本轮重测，不拼接历史记录。— 表示明确不支持或没有等价配置，原因逐项保留。串口通过不等于已完成目视防撕裂验收。</p></div>
<div class="nav">__NAV__</div><p><a href="comparison-current.xlsx">下载 Excel</a> · <a href="comparison-current.csv">原始字节 CSV</a> · <a href="README.md">测试口径与来源</a></p>
<div id="boards"></div><footer><p>FPS 为单轮16个有效场景的整数均值，不是物理刷新率；小幅差值不代表统计显著提升。</p><p>RGB/DSI 为在同一固件中切换双/三缓冲，公共硬件初始化统一创建3个driver framebuffer；这些数据不是各模式独立最小配置。内存仅显示四个时点的整机堆剩余，单位 KiB（1024 字节）；不代表组件独占占用或链接静态段。运行最小为各 heap 低水位之和，不保证同时发生。</p><p>首项打开前的框架边界不同：present 首次 LVGL 初始化在 open 内，adapter 在 bench_init 内。打开后、运行最小和关闭后更适合直接横向比较。关闭后不含整个框架反初始化。</p><p>两实现使用同一板卡、LVGL 场景和公共配置。所有原固件已恢复并整片校验，备份和编译目录不包含在本报告内。</p></footer></main>
<script id="data" type="application/json">__DATA__</script><script id="meta" type="application/json">__META__</script><script>
const data=JSON.parse(document.getElementById('data').textContent),meta=JSON.parse(document.getElementById('meta').textContent),labels=__LABELS__,order=__ORDER__;
const esc=x=>String(x).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const phases=[['before_open','打开前剩余'],['after_open','打开后剩余'],['after_run','运行最小剩余'],['after_close','关闭后剩余']];
function value(r,impl){const v=r?.[impl+'_lvgl_fps'];if(typeof v!=='number')return '—';let text=String(v);if(impl==='present'&&typeof r.adapter_lvgl_fps==='number'){let d=v-r.adapter_lvgl_fps;text+=`<span class="delta ${d<0?'down':''}">${d>0?'⬆️ +'+d:d<0?'⬇️ '+d:'＝ 0'}</span>`;}return text;}
function kib(r,impl,heap,phase){const v=r[`${impl}_${heap}_${phase}_free_bytes`];if(typeof v!=='number')return '—';let text=`<span class="memory-value">${(v/1024).toFixed(2)}</span>`;const other=r[`adapter_${heap}_${phase}_free_bytes`];if(impl==='present'&&typeof other==='number'){const d=v-other,amount=Math.abs(d)/1024,formatted=amount>0&&amount<0.01?amount.toFixed(3):amount.toFixed(2);text+=`<span class="memory-delta delta ${d<0?'down':d===0?'same':''}" data-delta-bytes="${d}" title="present − adapter；剩余内存越多越好">${d>0?'⬆️ +'+formatted:d<0?'⬇️ -'+formatted:'＝ 0.00'}</span>`;}return text;}
for(const b of order){const rs=data.filter(r=>r.board===b),modes=[...new Set(rs.map(r=>r.mode))],s=meta.boards[b];let note=b==='s31-qspi'?'C1/C2 × T1/T2 为 present 的 compose / tile 配置；adapter 原生同步整帧、异步整帧、异步 bounce 另列，不强行等价。同步 TE 非0°不支持软件旋转。TE gate 放行并不单凭串口证明没有扫描撕裂。':b==='box3-spi'?'SPI GRAM 不提供防撕裂保证。adapter NONE 非0°无软件旋转，AUTO 无对应入口；这些项明确跳过。':'只测 FULL / DIRECT / PARTIAL / AUTO 防撕裂路径，AUTO 无 adapter 等价入口。';let text=`<section class="board" id="${b}"><h2>${esc(labels[b])}</h2><p class="muted">MAC ${s.mac} · present ${s.present.pass} 通过 · adapter ${s.adapter.pass} 通过 / ${s.adapter.skip} 跳过 · 原固件恢复校验通过</p><p>${esc(note)}</p><div class="wrap"><table class="fps"><thead><tr><th rowspan="2">模式</th>${[0,90,180,270].map(a=>`<th colspan="2">${a}°</th>`).join('')}</tr><tr>${[0,90,180,270].map(()=>'<th>present</th><th>adapter</th>').join('')}</tr></thead><tbody>${modes.map(mode=>`<tr><td>${esc(mode)}</td>${[0,90,180,270].map(angle=>{let r=rs.find(x=>x.mode===mode&&x.rotation===angle);return `<td class="p">${value(r,'present')}</td><td class="a">${value(r,'adapter')}</td>`}).join('')}</tr>`).join('')}</tbody></table></div><details><summary>展开四时点内存对照 · RAM / PSRAM · KiB · 剩余越多越好</summary><div class="wrap"><table class="memory"><thead><tr><th>模式 / 角度</th><th>阶段</th><th>present RAM</th><th>adapter RAM</th><th>present PSRAM</th><th>adapter PSRAM</th></tr></thead><tbody>${rs.map(r=>phases.map(([phase,label],i)=>`<tr>${i===0?`<td rowspan="4">${esc(r.mode)} / ${r.rotation}°</td>`:''}<td class="phase">${label}</td>${['internal','psram'].map(heap=>['present','adapter'].map(impl=>`<td class="${impl==='present'?'p':'a'}">${kib(r,impl,heap,phase)}</td>`).join('')).join('')}</tr>`).join('')).join('')}</tbody></table></div></details></section>`;document.getElementById('boards').insertAdjacentHTML('beforeend',text);}
</script></html>'''
s=TEMPLATE.replace('__NAV__',''.join(f'<a href="#{b}">{html.escape(LABELS[b])}</a>' for b in ORDER)).replace('__DATA__',json.dumps(rows,ensure_ascii=False).replace('</','<\\/')).replace('__META__',json.dumps(meta,ensure_ascii=False).replace('</','<\\/')).replace('__LABELS__',json.dumps(LABELS,ensure_ascii=False)).replace('__ORDER__',json.dumps(ORDER))
if (OUT.parent/'present-s3-fb-iram-on-off-20261109/comparison.html').is_file():
 s=s.replace('<a href="comparison-current.xlsx">下载 Excel</a>', '<a href="../present-s3-fb-iram-on-off-20261109/comparison.html">S3 FB_IRAM 开关对照</a> · <a href="comparison-current.xlsx">下载 Excel</a>',1)
(OUT/'comparison-current.html').write_text(s)
lines=['# present / adapter 五板完整重测（2026-11-09）','','生产源码固定为 `b607bb9a1`；IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，无补丁；LVGL `9.6.0~1`。','', 'RGB / MIPI DSI NONE 不进入此次矩阵；mosaico 仅 TE；BOX-3 测 GRAM。每个有效组合一次完整16场景，不将失败前的成绩拼为通过。','', '| 板卡 | present 通过 | adapter 通过 | adapter 跳过 |','|---|---:|---:|---:|']
for b in ORDER:lines.append(f"| {LABELS[b]} | {summaries[b]['present']['pass']} | {summaries[b]['adapter']['pass']} | {summaries[b]['adapter']['skip']} |")
lines+=['','FPS为单轮16个有效场景的整数均值，不是物理刷新率；小幅差值不代表统计显著提升。120条配置/角度记录，108项present有效成绩，73项adapter有效成绩。所有数据来自本轮，两实现输入、LVGL和公共配置指纹相同。TE原生adapter与present C/T分别列出，不强行等价。','','## 内存口径','','RGB/DSI公共硬件初始化统一创建3个driver framebuffer，让同一固件能连续测试双/三缓冲；内存反映该公共基线，不能当作独立双缓冲的最小硬件占用。四时点为打开前剩余、打开后剩余、运行最小剩余、关闭后剩余；RAM为INTERNAL|8BIT，PSRAM为SPIRAM|8BIT，不重叠。CSV/JSON保留原始字节；网页/Excel使用KiB。运行最小为各heap低水位之和，不保证同时发生。不是组件独占占用，也不是链接时静态数据区归因。','','首项present在open内首次初始化LVGL，adapter在bench_init初始化；首项打开前边界不同，后三项更适合直接比较。关闭后不是全框架反初始化。','','## 硬件与验收','','S3更换为16MiB PSRAM的R16板，MAC `90:e5:b1:d0:a9:d8`，本轮不沿用旧8MiB板成绩。其余MAC见validation.json。五块板测试后均恢复运行前整片16MiB固件，并独立校验；Flash备份、固件与编译目录不放入交付件。','','Host ASan/UBSan 47/47通过。P4 adapter DIRECT 270°本轮完整16场景通过（23 FPS）；旧报告的失败记录保留，本轮一次通过不代表已经确定旧失败根因。串口完成不代表已完成目视防撕裂验收。mosaico TE的数据发送时间与gate等待独立记录，TE放行并不自动保证整个传输落在消隐期内。','','本轮显式用例表与收紧后的默认矩阵一致；默认 RGB/DSI 不含 NONE，显式专项仍可选择。测试规划器16项通过，统一runner的S31 RAM stub命令与本轮已验证运输命令一致，固件运行源码未因此更改。S31 RGB 的初次只读ROM备份因速度低主动停止，改用RAM stub完整重新读取。mosaico初次ROM读取在60KiB中断，未写测试固件；重试使用esptool5.4支持的RAM stub，仅调整下载运输方式，IDF/运行代码未改。初次失败日志保留。','','## 文件','','comparison-current.html：所有板卡默认并列，FPS和剩余内存差值都放在present单元格内；剩余越多越好，内存四时点直接并排。comparison-current.xlsx：同一份数据。comparison-current.csv / comparison-current.json：精确字节。raw-results.json：原始解析结果。manifests.json：构建/输入/固件指纹。validation.json：矩阵与恢复校验。']
(OUT/'README.md').write_text('\n'.join(lines)+'\n')
# Standalone spreadsheet, no dependency on office software.
NS='http://schemas.openxmlformats.org/spreadsheetml/2006/main';REL='http://schemas.openxmlformats.org/officeDocument/2006/relationships';ET.register_namespace('',NS)
def col(n):
 v=''
 while n:n,k=divmod(n-1,26);v=chr(65+k)+v
 return v
def sheet(data):
 root=ET.Element('{'+NS+'}worksheet')
 views=ET.SubElement(root,'{'+NS+'}sheetViews');view=ET.SubElement(views,'{'+NS+'}sheetView',workbookViewId='0')
 ET.SubElement(view,'{'+NS+'}pane',ySplit='1',topLeftCell='A2',activePane='bottomLeft',state='frozen')
 cols=ET.SubElement(root,'{'+NS+'}cols')
 for j in range(1,len(data[0])+1):
  width=120 if len(data[0])==1 else (34 if j==1 else 10 if j==2 else 24)
  ET.SubElement(cols,'{'+NS+'}col',min=str(j),max=str(j),width=str(width),customWidth='1')
 sd=ET.SubElement(root,'{'+NS+'}sheetData')
 for i,vals in enumerate(data,1):
  rr=ET.SubElement(sd,'{'+NS+'}row',r=str(i))
  for j,v in enumerate(vals,1):
   c=ET.SubElement(rr,'{'+NS+'}c',r=f'{col(j)}{i}')
   if isinstance(v,(int,float)) and not isinstance(v,bool):ET.SubElement(c,'{'+NS+'}v').text=str(v)
   else:c.set('t','inlineStr');ET.SubElement(ET.SubElement(c,'{'+NS+'}is'),'{'+NS+'}t').text=str(v)
 if len(data[0])>1:ET.SubElement(root,'{'+NS+'}autoFilter',ref=f'A1:{col(len(data[0]))}{len(data)}')
 return ET.tostring(root,encoding='utf-8',xml_declaration=True)
sheets=[('说明',[['说明']]+[[x] for x in lines if x])]
heads=['模式','角度','阶段','present FPS','adapter FPS','present RAM KiB','adapter RAM KiB','present PSRAM KiB','adapter PSRAM KiB']
for b in ORDER:
 data=[heads]
 for r in rows:
  if r['board']!=b:continue
  for phase,label in PHASES:
   vals=[r[f'{impl}_{heap}_{phase}_free_bytes'] for heap in ['internal','psram'] for impl in ['present','adapter']]
   vals=[float((Decimal(v)/1024).quantize(Decimal('.01'),rounding=ROUND_HALF_UP)) if isinstance(v,int) else v for v in vals]
   data.append([r['mode'],r['rotation'],label,r['present_lvgl_fps'],r['adapter_lvgl_fps'],*vals])
 sheets.append((b,data))
keys=list(rows[0]);sheets.append(('原始字节',[keys]+[[r[k] for k in keys] for r in rows]))
with zipfile.ZipFile(OUT/'comparison-current.xlsx','w',zipfile.ZIP_DEFLATED) as z:
 z.writestr('[Content_Types].xml','<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+''.join(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' for i in range(1,len(sheets)+1))+'</Types>')
 z.writestr('_rels/.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="'+REL+'/officeDocument" Target="xl/workbook.xml"/></Relationships>')
 wb=ET.Element('{'+NS+'}workbook');ss=ET.SubElement(wb,'{'+NS+'}sheets')
 for i,(name,data) in enumerate(sheets,1):ET.SubElement(ss,'{'+NS+'}sheet',name=name,sheetId=str(i),attrib={'{'+REL+'}id':f'rId{i}'});z.writestr(f'xl/worksheets/sheet{i}.xml',sheet(data))
 z.writestr('xl/workbook.xml',ET.tostring(wb,encoding='utf-8',xml_declaration=True));z.writestr('xl/_rels/workbook.xml.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+''.join(f'<Relationship Id="rId{i}" Type="{REL}/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(sheets)+1))+'</Relationships>')
(OUT/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in sorted(OUT.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
print(json.dumps(meta,ensure_ascii=False,indent=2))
