from pathlib import Path
import importlib.util,json,re,html,csv,datetime
from zoneinfo import ZoneInfo
R=Path(__file__).parent;MODES=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];ANGLES=[0,90,180,270];BOARDS=['s3-rgb','s31-rgb','p4-dsi'];IMPL=['present','adapter'];LABELS={'s3-rgb':'S3 RGB · 480×480 · R16','s31-rgb':'S31 Korvo RGB · 800×480','p4-dsi':'P4 MIPI DSI · 1024×600'}
S=Path('/tmp/present-xip-on-clean-source-20261110/components/display/tools/esp_display_present');spec=importlib.util.spec_from_file_location('runner_cmp',S/'dev/tools/run.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
E=html.escape;data={'xip':'on','angles':ANGLES,'fresh_full_run':True,'source_commit':'c71871f433e34dc456970fb99702362b9a34f213','updated':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),'boards':{},'summary':{'clean_pass':0,'diagnostic_pass':0,'skip':0,'address_hits':0,'errors':[]},'methods':json.loads((R/'diagnostic-source.json').read_text())['methods']}
for board in BOARDS:
 b=data['boards'][board]={'cohorts':{}};state=R/board/'controller.json';b['controller']=json.loads(state.read_text()) if state.exists() else {}
 for cohort,trace in [('clean',False),('diagnostic',True)]:
  b['cohorts'][cohort]={}
  for impl in IMPL:
   v=R/board/'benchmark'/(impl+'-xip-on'+('-driver-trace' if trace else ''));text=(v/'live.log').read_text(errors='replace') if (v/'live.log').exists() else '';parsed=runner.parse_log(text)
   addresses={};supplies={};events=[];scene_scores={}
   for line in text.splitlines():
    if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
     d=dict(re.findall(r'(\w+)=([^ ]+)',line));d={k:int(value) if value.isdecimal() else value for k,value in d.items()};key=(d['mode'],d['rotation'])
     if line.startswith('RGB_ADDRESS '):addresses[key]=d
     elif line.startswith('RGB_SUPPLY '):supplies[key]=d
     else:events.append(d)
    m=re.search(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',line)
    if m:scene_scores.setdefault((m[1],int(m[2])),[]).append({'name':m[3],'fps':int(m[4])})
   lookup={(x['mode'],x['rotation']):x for x in parsed['rows']};rows=[]
   for mode in MODES:
    for angle in ANGLES:
     key=(mode,angle);x=lookup.get(key,{'mode':mode,'rotation':angle,'status':'pending'})
     if mode=='AUTO' and impl=='adapter' and x['status']=='pending':x['status']='planned_skip'
     x['scene_scores']=scene_scores.get(key,[])
     if trace:x.update(address=addresses.get(key),supply=supplies.get(key),address_events=[e for e in events if (e['mode'],e['rotation'])==key])
     if x['status']=='PASS':
      data['summary'][cohort+'_pass']+=1
      if trace and x.get('address'):data['summary']['address_hits']+=sum(x['address'].get(k,0) for k in ['release_active','write_active','confirm_wrong'])
     if x['status']=='SKIP':data['summary']['skip']+=1
     rows.append(x)
   b['cohorts'][cohort][impl]={'rows':rows,'complete':parsed['complete'],'errors':parsed['errors'],'manifest':json.loads((v/'manifest.json').read_text()) if (v/'manifest.json').exists() else None,'result':json.loads((v/'results.json').read_text()) if (v/'results.json').exists() else None,'source':str(v)}
   data['summary']['errors'] +=[{'board':board,'cohort':cohort,'implementation':impl,'error':x} for x in parsed['errors']]
data['complete']=data['summary']['clean_pass']==132 and data['summary']['diagnostic_pass']==132 and data['summary']['skip']==24 and not data['summary']['errors'] and all(data['boards'][b]['controller'].get('success') for b in BOARDS)
(R/'matrix-data.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def row(board,cohort,impl,mode,angle):return next(x for x in data['boards'][board]['cohorts'][cohort][impl]['rows'] if (x['mode'],x['rotation'])==(mode,angle))
def number(value,decimals=0):return f'<span class="value" data-value="{value}" data-decimals="{decimals}">{value:.{decimals}f}</span>'
def arrow(p,a,decimals=0):
 d=p-a;n=3 if decimals==2 and 0<abs(d)<0.01 else decimals
 return f'<span class="delta {"up" if d>0 else "down" if d<0 else "same"}" data-delta="{d}" data-decimals="{n}">'+('⬆️ +' if d>0 else '⬇️ −' if d<0 else '＝ ')+f'{abs(d):.{n}f}</span>'
def fps(b,i,m,a):
 x=row(b,'clean',i,m,a)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 if x['status']!='PASS':return '待完成'
 out=number(x['lvgl_fps']);other=row(b,'clean','adapter',m,a)
 if i=='present' and other['status']=='PASS':out+=arrow(x['lvgl_fps'],other['lvgl_fps'])
 return out
def free(b,i,m,a,h,p):
 x=row(b,'clean',i,m,a)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 if x['status']!='PASS':return '待完成'
 k=h+('_min_free' if p=='after_run' else '_free');v=x['memory'][p][k]/1024;out=number(v,2);other=row(b,'clean','adapter',m,a)
 if i=='present' and other['status']=='PASS':out+=arrow(v,other['memory'][p][k]/1024,2)
 return out
def diag(b,i,m,ang):
 x=row(b,'diagnostic',i,m,ang)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 a=x.get('address')
 if not a:return '待完成'
 hits=sum(a[k] for k in ['release_active','write_active','confirm_wrong']);checks=sum(a[k] for k in ['releases','writes','confirms']);out=f'<b class="{"alert" if hits else "neutral"}">'+number(hits)+f' 次地址命中 / {checks} 次检查</b>'
 release=str(a['release_active'])+'/'+str(a['releases']) if a['releases'] else '此路径不适用';confirm=str(a['confirm_wrong'])+'/'+str(a['confirms']) if a['confirms'] else '此路径不适用'
 out+=f'<small>交还 {release} · 可写 {a["write_active"]}/{a["writes"]} · 确认 {confirm}</small><small>未知 {a["raced"]} · 池外 {a["not_pool"]} · RUN边界 {a["boundaries"]}</small>'
 s=x.get('supply')
 if b=='s3-rgb' and s and s['expected_eofs']:out+=f'<small>供数不足 {s["measured_short"]}/{s["measured_frames"]} 帧 · 既有重启 {s["measured_restarts"]}</small>'
 elif b=='s3-rgb':out+='<small>供数检测未覆盖</small>'
 else:out+=f'<small>硬件underrun中断 {a["underruns"]} 次</small>'
 ev=x.get('address_events',[])
 if any(e['request_seq']==0 for e in ev):out+='<small class="alert">含首次提交前命中，需核对重新打开的初始FB基线</small>'
 if ev:out+='<details><summary>地址样本（最多16条）</summary><pre>'+E('\n'.join(json.dumps(e,ensure_ascii=False) for e in ev))+'</pre></details>'
 return out
css='''*{box-sizing:border-box}body{margin:0;background:#f4f6fa;color:#1c2f46;font:14px/1.65 system-ui,sans-serif}main{max-width:1680px;margin:auto;padding:24px}h1{font-size:27px}h2{font-size:21px;margin:0}p{margin:8px 0}.notice,.board{background:white;padding:18px;border:1px solid #d9e1ec;border-radius:12px;margin:18px 0}.notice{background:#eaf0fa}.nav{display:flex;gap:12px;flex-wrap:wrap}.nav a{padding:6px 10px;background:white;border-radius:6px}.wrap{overflow-x:auto}table{border-collapse:collapse;width:100%;margin:14px 0}th,td{padding:9px 11px;border-bottom:1px solid #dce3eb;text-align:right;vertical-align:top;white-space:nowrap}th{background:#f0f3f8}th:first-child,td:first-child{text-align:left}.p{background:#eaf6f0}.a{background:#eef1fb}.delta{display:inline-block;margin-left:7px;font-size:12px}.up{color:#187549}.down,.alert{color:#b33b26}.same,.neutral{color:#4a5c73}small{display:block;font-size:12px;color:#627186}.diag td{white-space:normal;text-align:left;min-width:260px}.diag th:first-child{min-width:150px}details{margin:12px 0}summary{font-weight:600;cursor:pointer}pre{font-size:11px;white-space:pre-wrap;word-break:break-all;max-width:580px}a{color:#215e99}footer{color:#586b82}@media(max-width:760px){main{padding:10px}.board{padding:12px}h1{font-size:23px}th,td{padding:7px}}'''
s=data['summary'];body='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>RGB / MIPI · XIP ON · 四角度正式对比</title><style>'+css+'</style><main><h1>RGB / MIPI · XIP ON · 四角度正式对比</h1><div class="notice"><b>'+('本轮采集完成' if data['complete'] else '本轮采集中')+f'：性能 {s["clean_pass"]}/132 项，诊断 {s["diagnostic_pass"]}/132 项</b><p>0°、90°、180°、270°均重新完整运行；每项16场景，合计4224场景。全部开启XIP，排除NONE；adapter不支持AUTO记−。不使用上一轮0°或XIP OFF成绩补表。</p><p>正式帧率/内存来自干净驱动轮；交接检测来自独立轻量诊断轮，只记录，不改交换、不assert。地址检查点命中 '+str(s['address_hits'])+' 次，不能直接当作撕裂次数。</p><p>内存四个时点均为剩余量，单位KiB。箭头在present列，与adapter比较；帧率、剩余内存均越多越好。运行最小是该项局部低水位，RAM与PSRAM最低点不一定同时发生。</p><p>更新时间 '+E(data['updated'])+'</p></div><nav class="nav">'+''.join('<a href="#'+b+'">'+E(LABELS[b])+'</a>' for b in BOARDS)+'</nav>'
PHASES=[('before_open','打开前剩余'),('after_open','打开后剩余'),('after_run','运行最小剩余'),('after_close','关闭后剩余')]
for b in BOARDS:
 body+='<section class="board" id="'+b+'"><h2>'+E(LABELS[b])+'</h2><p>'+{'s3-rgb':'RGB bounce 10行 · panel池3张FB · XIP ON · present FB_IRAM开启 · 32KiB指令/64KiB数据缓存','s31-rgb':'RGB 非bounce · AXI link-switch · panel池3张FB · XIP ON','p4-dsi':'MIPI DSI · DW GDMA · panel池3张FB · XIP ON'}[b]+'</p><nav class="nav"><a href="#'+b+'-fps">帧率</a><a href="#'+b+'-memory">内存</a><a href="#'+b+'-diag">交接检测</a></nav><h3 id="'+b+'-fps">帧率 · LVGL 综合分</h3><div class="wrap"><table class="fps"><thead><tr><th rowspan="2">模式</th>'+''.join(f'<th colspan="2">{a}°</th>' for a in ANGLES)+'</tr><tr>'+''.join('<th>present</th><th>adapter</th>' for a in ANGLES)+'</tr></thead><tbody>'
 for m in MODES:body+='<tr><td>'+m+'</td>'+''.join('<td class="'+('p' if i=='present' else 'a')+'">'+fps(b,i,m,a)+'</td>' for a in ANGLES for i in IMPL)+'</tr>'
 body+='</tbody></table></div><small>LVGL完整16场景综合分，不等于屏幕扫描Hz；adapter旧transport计数不作为FPS依据。</small><h3 id="'+b+'-memory">内存剩余 · 干净驱动 · KiB</h3><div class="wrap"><table class="memory"><thead><tr><th>模式 · 角度</th><th>时点</th><th>present RAM</th><th>adapter RAM</th><th>present PSRAM</th><th>adapter PSRAM</th></tr></thead><tbody>'
 for m in MODES:
  for a in ANGLES:
   for j,(p,label) in enumerate(PHASES):body+='<tr>'+('<td rowspan="4">'+m+f' · {a}°</td>' if j==0 else '')+'<td>'+label+'</td>'+''.join('<td class="'+('p' if i=='present' else 'a')+'">'+free(b,i,m,a,h,p)+'</td>' for h in ['internal','psram'] for i in IMPL)+'</tr>'
 body+='</tbody></table></div><h3 id="'+b+'-diag">交接异常 · 独立轻量诊断轮</h3><p>'+E(data['methods'][b])+'</p><div class="wrap"><table class="diag"><thead><tr><th>模式 · 角度</th><th>present</th><th>adapter</th></tr></thead><tbody>'
 for m in MODES:
  for a in ANGLES:body+='<tr><td>'+m+f' · {a}°</td><td class="p">'+diag(b,'present',m,a)+'</td><td class="a">'+diag(b,'adapter',m,a)+'</td></tr>'
 body+='</tbody></table></div><details><summary>逐场景完整分数 · 干净驱动</summary><div class="wrap"><table><tr><th>模式 · 角度</th><th>场景</th><th>present FPS</th><th>adapter FPS</th></tr>'
 for m in MODES:
  for ang in ANGLES:
   p={x['name']:x['fps'] for x in row(b,'clean','present',m,ang)['scene_scores']};a={x['name']:x['fps'] for x in row(b,'clean','adapter',m,ang)['scene_scores']}
   for n in dict.fromkeys([*p,*a]):body+='<tr><td>'+m+f' · {ang}°</td><td>'+E(n)+'</td><td>'+str(p.get(n,'−'))+'</td><td>'+str(a.get(n,'−'))+'</td></tr>'
 body+='</table></div></details></section>'
body+='<footer><p>S3观察实际bounce源；S31观察DMA预取描述符所属FB；P4观察实际装载帧的独立快照。口径分开解释，后两者不等同逐像素扫描证据。零命中只表示已覆盖检查点未检出，不能保证全部CPU/DMA写入安全。adapter仅0°双缓冲FULL/DIRECT走独立确认等待；旋转及其他模式走pipeline交还，其独立确认项标为不适用，不当作零异常。未知源单列，不能当作零异常。累计计数不截断，每项最多存前16条样本。同一次提前交接可能命中多个检查点，不等于多次撕裂。</p><p>诊断仅RUN窗口，丢弃入口首半帧；不含open/close，但包含结束统计日志。S3供数不足与既有自动重启、S31/P4硬件underrun分别计数，不混作地址异常。</p><p><a href="comparison.csv">CSV</a> · <a href="matrix-data.json">完整数据与来源</a> · <a href="findings.md">本轮检测结果</a> · <a href="README.md">测试口径</a></p></footer></main></html>'
(R/'comparison.html').write_text(body)
fields=['board','mode','angle','implementation','clean_status','lvgl_fps','internal_before_open','internal_after_open','internal_min','internal_after_close','psram_before_open','psram_after_open','psram_min','psram_after_close','diagnostic_status','checks','release_active','write_active','confirm_wrong','unknown','run_boundaries','supply_short','restarts','underruns']
with (R/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
 for b in BOARDS:
  for m in MODES:
   for a in ANGLES:
    for i in IMPL:
     p=row(b,'clean',i,m,a);d=row(b,'diagnostic',i,m,a);x={'board':b,'mode':m,'angle':a,'implementation':i,'clean_status':p['status'],'diagnostic_status':d['status']}
     if p['status']=='PASS':
      x['lvgl_fps']=p['lvgl_fps']
      for h in ['internal','psram']:
       for phase,suffix in [('before_open','before_open'),('after_open','after_open'),('after_run','min'),('after_close','after_close')]:x[h+'_'+suffix]=p['memory'][phase][h+('_min_free' if phase=='after_run' else '_free')]
     addr=d.get('address');s=d.get('supply')
     if addr:
      x.update(checks=sum(addr[k] for k in ['releases','writes','confirms']),release_active=addr['release_active'],write_active=addr['write_active'],confirm_wrong=addr['confirm_wrong'],unknown=addr['raced'],run_boundaries=addr['boundaries'])
      if b!='s3-rgb':x['underruns']=addr['underruns']
     if b=='s3-rgb' and s and s['expected_eofs']:x.update(supply_short=s['measured_short'],restarts=s['measured_restarts'])
     w.writerow(x)
print(json.dumps({'complete':data['complete'],**data['summary']},ensure_ascii=False))
