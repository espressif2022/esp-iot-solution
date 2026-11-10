from pathlib import Path
import importlib.util,json,re,html,csv,datetime
from zoneinfo import ZoneInfo
R=Path('/tmp/present-xip-on-comparison-20261110');MODES=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'];BOARDS=['s3-rgb','s31-rgb','p4-dsi'];IMPL=['present','adapter'];LABELS={'s3-rgb':'S3 RGB · 480×480 · R16','s31-rgb':'S31 Korvo RGB · 800×480','p4-dsi':'P4 MIPI DSI · 1024×600'}
S=Path('/tmp/present-xip-on-clean-source-20261110/components/display/tools/esp_display_present');spec=importlib.util.spec_from_file_location('runner_cmp',S/'dev/tools/run.py');runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
E=html.escape;data={'xip':'on','angles':[0],'updated':datetime.datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),'boards':{},'summary':{'clean_pass':0,'diagnostic_pass':0,'skip':0,'address_hits':0,'errors':[]},'methods':json.loads((R/'diagnostic-source.json').read_text())['methods']}
for board in BOARDS:
 b=data['boards'][board]={'cohorts':{}}
 state=R/board/'controller.json';b['controller']=json.loads(state.read_text()) if state.exists() else {}
 for cohort,trace in [('clean',False),('diagnostic',True)]:
  b['cohorts'][cohort]={}
  for impl in IMPL:
   v=R/board/'benchmark'/(impl+'-xip-on'+('-driver-trace' if trace else ''));text=(v/'live.log').read_text(errors='replace') if (v/'live.log').exists() else '';parsed=runner.parse_log(text)
   addresses={};supplies={};events=[];scene_scores={}
   for line in text.splitlines():
    if line.startswith(('RGB_ADDRESS ','RGB_SUPPLY ','RGB_ADDRESS_EVENT ')):
     d=dict(re.findall(r'(\w+)=([^ ]+)',line));d={k:int(value) if value.isdecimal() else value for k,value in d.items()}
     if line.startswith('RGB_ADDRESS '):addresses[d['mode']]=d
     elif line.startswith('RGB_SUPPLY '):supplies[d['mode']]=d
     else:events.append(d)
    match=re.search(r'SCENE mode=(\w+) rotation=0 name="([^"]+)" fps=(\d+)',line)
    if match:scene_scores.setdefault(match[1],[]).append({'name':match[2],'fps':int(match[3])})
   lookup={x['mode']:x for x in parsed['rows']};rows=[]
   for mode in MODES:
    row=lookup.get(mode,{'mode':mode,'rotation':0,'status':'pending'})
    if mode=='AUTO' and impl=='adapter' and row['status']=='pending':row={'mode':mode,'rotation':0,'status':'planned_skip'}
    row['scene_scores']=scene_scores.get(mode,[])
    if trace:row['address']=addresses.get(mode);row['supply']=supplies.get(mode);row['address_events']=[x for x in events if x['mode']==mode]
    if row['status']=='PASS':
     data['summary'][cohort+'_pass']+=1
     if trace and row.get('address'):data['summary']['address_hits']+=sum(row['address'].get(k,0) for k in ['release_active','write_active','confirm_wrong'])
    if row['status']=='SKIP':data['summary']['skip']+=1
    rows.append(row)
   manifest=json.loads((v/'manifest.json').read_text()) if (v/'manifest.json').exists() else None
   result=json.loads((v/'results.json').read_text()) if (v/'results.json').exists() else None
   b['cohorts'][cohort][impl]={'rows':rows,'complete':parsed['complete'],'errors':parsed['errors'],'manifest':manifest,'result':result,'source':str(v)}
   data['summary']['errors'] +=[{'board':board,'cohort':cohort,'implementation':impl,'error':x} for x in parsed['errors']]
data['complete']=data['summary']['clean_pass']==33 and data['summary']['diagnostic_pass']==33 and data['summary']['skip']==6 and all(data['boards'][b]['controller'].get('success') for b in BOARDS)
(R/'matrix-data.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def row(board,cohort,impl,mode):return next(x for x in data['boards'][board]['cohorts'][cohort][impl]['rows'] if x['mode']==mode)
def arrow(p,a,unit='',decimals=0):
 delta=p-a;v=abs(delta);fmt=f'{v:.{3 if decimals==2 and 0<v<0.01 else decimals}f}';return '<span class="delta '+('up' if delta>0 else 'down' if delta<0 else 'same')+'">'+('⬆️ +'+fmt if delta>0 else '⬇️ −'+fmt if delta<0 else '＝ 0')+unit+'</span>'
def fps(board,impl,mode):
 x=row(board,'clean',impl,mode)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 if x['status']!='PASS':return '<span class="pending">待完成</span>'
 s=str(x['lvgl_fps']);a=row(board,'clean','adapter',mode)
 if impl=='present' and a['status']=='PASS':s+=arrow(x['lvgl_fps'],a['lvgl_fps'])
 return s
def free(board,impl,mode,heap,phase):
 x=row(board,'clean',impl,mode)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 if x['status']!='PASS':return '<span class="pending">待完成</span>'
 key=heap+('_min_free' if phase=='after_run' else '_free');value=x['memory'][phase][key];s=f'<span class="memory-value">{value/1024:.2f}</span>';a=row(board,'clean','adapter',mode)
 if impl=='present' and a['status']=='PASS':
  other=a['memory'][phase][key];s+=arrow(value/1024,other/1024,'',2)
 return s
def diag(board,impl,mode):
 x=row(board,'diagnostic',impl,mode)
 if x['status'] in ['SKIP','planned_skip']:return '−'
 a=x.get('address')
 if not a:return '<span class="pending">待完成</span>'
 hits=sum(a[k] for k in ['release_active','write_active','confirm_wrong']);checks=sum(a[k] for k in ['releases','writes','confirms']);coverage='有效' if checks else '未覆盖'
 out=f'<b class="{"alert" if hits else "neutral"}">{hits} 次地址命中 / {checks} 次检查</b>'
 events=x.get('address_events',[])
 if events and hits==len(events):out+='<small>完整样本涉及 '+str(len({v['request_seq'] for v in events}))+' 笔提交；命中数按检查点累计</small>'
 out+=f'<small>交还 {a["release_active"]}/{a["releases"]} · 可写 {a["write_active"]}/{a["writes"]} · 确认 {a["confirm_wrong"]}/{a["confirms"]}</small>'
 out+=f'<small>未知 {a["raced"]} · 独立输入/池外 {a["not_pool"]} · {a["boundaries"]} 个运行期边界</small>'
 s=x.get('supply')
 if board=='s3-rgb' and s and s['expected_eofs']:out+=f'<small>供数不足 {s["measured_short"]}/{s["measured_frames"]} 帧 · 既有重启 {s["measured_restarts"]}</small>'
 elif board=='s3-rgb':out+='<small>供数检测：未覆盖</small>'
 else:out+=f'<small>硬件 underrun 中断 {a["underruns"]} 次 · S3补数/重启计数：不适用</small>'
 if board=='p4-dsi' and any(ev.get('request_seq')==0 for ev in events):out+='<small class="alert">含本模式首次提交前的命中，需核对共享panel重新打开时的初始FB基线</small>'
 if x.get('address_events'):out+='<details><summary>原始地址样本</summary><pre>'+E('\n'.join(json.dumps(ev,ensure_ascii=False) for ev in x['address_events']))+'</pre></details>'
 return out
css='''*{box-sizing:border-box}body{margin:0;background:#f4f6fa;color:#1c2f46;font:14px/1.65 system-ui,sans-serif}main{max-width:1500px;margin:auto;padding:24px}h1{font-size:27px}h2{font-size:21px;margin:0}p{margin:8px 0}.notice,.board{background:white;padding:18px;border:1px solid #d9e1ec;border-radius:12px;margin:18px 0}.notice{background:#eaf0fa}.nav{display:flex;gap:12px;flex-wrap:wrap}.nav a{padding:6px 10px;background:white;border-radius:6px}.wrap{overflow-x:auto}table{border-collapse:collapse;width:100%;margin:14px 0}th,td{padding:9px 11px;border-bottom:1px solid #dce3eb;text-align:right;vertical-align:top;white-space:nowrap}th{background:#f0f3f8}th:first-child,td:first-child{text-align:left}.p{background:#eaf6f0}.a{background:#eef1fb}.delta{display:inline-block;margin-left:7px;font-size:12px}.up{color:#187549}.down,.alert{color:#b33b26}.same,.neutral{color:#4a5c73}.pending{color:#906f13}small{display:block;font-size:12px;color:#627186}.diag td{white-space:normal;text-align:left;min-width:260px}.diag th:first-child{min-width:150px}details{margin:12px 0}summary{font-weight:600;cursor:pointer}pre{font-size:11px;white-space:pre-wrap;word-break:break-all;max-width:580px}a{color:#215e99}footer{color:#586b82}@media(max-width:760px){main{padding:10px}.board{padding:12px}h1{font-size:23px}th,td{padding:7px}}'''
body='<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>RGB / MIPI · XIP ON · present / adapter</title><style>'+css+'</style><main><h1>RGB / MIPI · XIP ON · present / adapter 对比</h1>'
s=data['summary'];body+='<div class="notice"><b>'+('本轮采集完成' if data['complete'] else '本轮采集中')+f'：性能 {s["clean_pass"]}/33 项，诊断 {s["diagnostic_pass"]}/33 项</b><p class="alert">本轮已记录 '+str(s['address_hits'])+' 个地址检查点命中，详见诊断表；采集完成不代表无异常。</p><p>只测 XIP 开启，0°，每项完整16场景。NONE不纳入，adapter AUTO记−。帧率/内存来自干净驱动；交接异常来自独立轻量诊断轮。两轮不混成绩，正式运行不做像素指纹检查。</p><p>内存单位KiB，四个时点都是“剩余”：打开前、打开后、运行最小、关闭后。箭头写在present列，与adapter比较；帧率、剩余内存均越多越好。运行最小为该项局部低水位，RAM与PSRAM的最低点不一定同时发生。</p><p>当前比较不再包含XIP OFF，也不混入此前多角度或其他接口成绩。更新时间 '+E(data['updated'])+'</p></div><nav class="nav">'+''.join('<a href="#'+b+'">'+E(LABELS[b])+'</a>' for b in BOARDS)+'</nav>'
PHASES=[('before_open','打开前剩余'),('after_open','打开后剩余'),('after_run','运行最小剩余'),('after_close','关闭后剩余')]
for b in BOARDS:
 body+='<section class="board" id="'+b+'"><h2>'+E(LABELS[b])+'</h2><p>'+{'s3-rgb':'RGB bounce 10行 · 双方panel池3张FB · XIP ON · present FB_IRAM开启 · 32KiB指令/64KiB数据缓存','s31-rgb':'RGB 非bounce · AXI link-switch · 双方panel池3张FB · XIP ON','p4-dsi':'MIPI DSI · DW GDMA · 双方panel池3张FB · XIP ON'}[b]+'</p><h3>帧率 · LVGL 综合分</h3><div class="wrap"><table class="fps"><tr><th>模式 · 0°</th><th>present</th><th>adapter</th></tr>'
 for mode in MODES:body+='<tr><td>'+mode+'</td><td class="p">'+fps(b,'present',mode)+'</td><td class="a">'+fps(b,'adapter',mode)+'</td></tr>'
 body+='</table></div><small>采用LVGL完整16场景综合分，不等于屏幕扫描Hz。adapter的旧transport帧计数不作为FPS依据。</small><h3>内存剩余 · 干净驱动 · KiB</h3><div class="wrap"><table class="memory"><tr><th>模式</th><th>时点</th><th>present RAM</th><th>adapter RAM</th><th>present PSRAM</th><th>adapter PSRAM</th></tr>'
 for mode in MODES:
  for j,(phase,label) in enumerate(PHASES):
   body+='<tr>'+('<td rowspan="4">'+mode+'</td>' if j==0 else '')+'<td>'+label+'</td>'+''.join('<td class="'+('p' if impl=='present' else 'a')+'">'+free(b,impl,mode,heap,phase)+'</td>' for heap in ['internal','psram'] for impl in IMPL)+'</tr>'
 body+='</table></div><h3>交接异常 · 独立轻量诊断轮</h3><p>'+E(data['methods'][b])+'</p><div class="wrap"><table class="diag"><tr><th>模式 · 0°</th><th>present</th><th>adapter</th></tr>'
 for mode in MODES:body+='<tr><td>'+mode+'</td><td class="p">'+diag(b,'present',mode)+'</td><td class="a">'+diag(b,'adapter',mode)+'</td></tr>'
 body+='</table></div><details><summary>逐场景完整分数 · 干净驱动</summary><div class="wrap"><table><tr><th>模式</th><th>场景</th><th>present FPS</th><th>adapter FPS</th></tr>'
 for mode in MODES:
  p={x['name']:x['fps'] for x in row(b,'clean','present',mode)['scene_scores']};a={x['name']:x['fps'] for x in row(b,'clean','adapter',mode)['scene_scores']}
  for name in list(dict.fromkeys([*p,*a])):body+='<tr><td>'+mode+'</td><td>'+E(name)+'</td><td>'+str(p.get(name,'−'))+'</td><td>'+str(a.get(name,'−'))+'</td></tr>'
 body+='</table></div></details></section>'
body+='<footer><p>地址命中表示监测到的交还/取得可写/确认边界与驱动取数或DMA描述符借用地址有冲突，需要结合样本解释。S3使用实际bounce源；S31使用预取描述符；P4使用实际编程帧的独立快照。三者不能冒充相同的逐像素硬件扫描证据。</p><p>地址命中按检查点累计，同一次提前交接可能产生确认、绘制两条记录，不等于独立撕裂次数。累计计数不截断，最多保存前16条地址样本。零命中只表示本轮监测点未检出，不覆盖任意CPU/DMA写入与全部肉眼异常。未知地址单列，不当成零异常；S31预取与P4编程地址也不能单独证明像素输出完整。S3补数不足、既有自动重启及其他板硬件underrun分别记录。诊断窗口不含open/close，但包含benchmark结束统计日志。</p><p><a href="prior-s3-direct-findings.md">前一轮XIP ON的S3 adapter DIRECT地址冲突记录</a>（不混入本轮计数；未实施交接修复） · <a href="comparison.csv">帧率/内存/诊断CSV</a> · <a href="matrix-data.json">完整数据与来源</a> · <a href="README.md">测试口径与验证</a></p></footer></main></html>'
(R/'comparison.html').write_text(body)
fields=['board','mode','angle','implementation','clean_status','lvgl_fps','internal_before_open','internal_after_open','internal_min','internal_after_close','psram_before_open','psram_after_open','psram_min','psram_after_close','diagnostic_status','checks','release_active','write_active','confirm_wrong','unknown','run_boundaries','supply_short','restarts','underruns']
with (R/'comparison.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
 for b in BOARDS:
  for mode in MODES:
   for impl in IMPL:
    p=row(b,'clean',impl,mode);d=row(b,'diagnostic',impl,mode);x={'board':b,'mode':mode,'angle':0,'implementation':impl,'clean_status':p['status'],'diagnostic_status':d['status']}
    if p['status']=='PASS':
     x['lvgl_fps']=p['lvgl_fps']
     for heap in ['internal','psram']:
      for phase,suffix in [('before_open','before_open'),('after_open','after_open'),('after_run','min'),('after_close','after_close')]:x[heap+'_'+suffix]=p['memory'][phase][heap+('_min_free' if phase=='after_run' else '_free')]
    a=d.get('address');s=d.get('supply')
    if a:
     x.update(checks=sum(a[k] for k in ['releases','writes','confirms']),release_active=a['release_active'],write_active=a['write_active'],confirm_wrong=a['confirm_wrong'],unknown=a['raced'],run_boundaries=a['boundaries'])
     if b!='s3-rgb':x['underruns']=a['underruns']
    if b=='s3-rgb' and s and s['expected_eofs']:x.update(supply_short=s['measured_short'],restarts=s['measured_restarts'])
    w.writerow(x)
print(json.dumps({'complete':data['complete'],**data['summary']},ensure_ascii=False))
