from pathlib import Path
import json,csv,sys,importlib.util,hashlib,html,zipfile,re
from xml.etree import ElementTree as ET
b=Path('/tmp/present-all-modes-20261107');out=b/'report';out.mkdir(exist_ok=True)
runner=b/'source/components/display/tools/esp_display_present/dev/tools/run.py'
spec=importlib.util.spec_from_file_location('runner',runner);r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
def failed_memory(path):
 memory={}
 for line in path.read_text(errors='replace').splitlines():
  m=re.search(r'MEMORY mode=\S+ rotation=\d+ phase=(\S+)',line)
  if m and m[1] in ['before_open','after_open','after_run','after_close']:
   memory[m[1]]={k:int(v) for k,v in re.findall(r'(internal_total|internal_free|psram_total|psram_free|internal_min_free|psram_min_free)=(\d+)',line)}
 return memory
boards=['s3-rgb','p4-dsi','box3-spi','s31-qspi'];rows=[];checks=[];raw=[]
for board in boards:
 results={};manifests={}
 for impl in ['present','adapter'] if board!='s31-qspi' else ['present']:
  p=b/board/'benchmark'/impl
  canonical = b/'p4-adapter-continuous-attempt/manifest.json' if board=='p4-dsi' and impl=='adapter' else p/'manifest.json'
  manifests[impl]=json.loads(canonical.read_text())
  if board == 'p4-dsi' and impl == 'present' and (b/'p4-isolated-cases').exists():
   initial = r.parse_log((p/'live.log').read_text(errors='replace'))
   follow = r.parse_log((b/'p4-followup-attempt/live.log').read_text(errors='replace'))
   assert initial['errors'] and follow['errors']
   keep = [dict(v,measurement_segment='initial') for v in initial['rows'] if v['mode'] in ['NONE','DOUBLE_FULL','TRIPLE_FULL']]
   keep += [dict(v,measurement_segment='followup') for v in follow['rows']]
   assert len(keep)==17
   fm=json.loads((b/'p4-followup-attempt/manifest.json').read_text())
   comparekeys=['board','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','test_sources_sha256','component_sources_sha256','memory_schema']
   for key in comparekeys:assert fm[key]==manifests[impl][key],('followup',key)
   isolated=sorted((b/'p4-isolated-cases').iterdir());assert len(isolated)==11
   for d in isolated:
    cm=json.loads((d/'manifest.json').read_text());case=cm['cases'][0]
    assert len(cm['cases'])==1
    assert case in manifests[impl]['cases']
    for key in comparekeys:assert cm[key]==manifests[impl][key],('isolated',d,key)
    result=json.loads((d/'results.json').read_text())
    valid=result['complete'] and result['matrix_complete'] and result.get('memory_complete') and not result['errors']
    if valid:
     assert len(result['rows'])==1
     value=dict(result['rows'][0],measurement_segment='isolated/'+d.name)
    else:
     value=dict(status='FAIL',mode=case['name'],rotation=case['rotation'],measurement_segment='isolated/'+d.name,failure_errors=result['errors'] or ['Incomplete benchmark; see capture log and completion flags'],memory=failed_memory(d/'live.log'),memory_partial=True)
    keep.append(value)
   results[impl]=dict(complete=True,matrix_complete=True,memory_complete=True,errors=[],rows=keep)
   (out/'p4-initial-attempt-errors.json').write_text(json.dumps(initial,indent=2)+'\n')
   (out/'p4-followup-attempt-errors.json').write_text(json.dumps(follow,indent=2)+'\n')
  elif board=='p4-dsi' and impl=='adapter' and (b/'p4-adapter-isolated-cases').exists():
   initial=r.parse_log((b/'p4-adapter-continuous-attempt/live.log').read_text(errors='replace'))
   timeout=json.loads((b/'p4-adapter-continuous-timeout.json').read_text())
   keep=[dict(v,measurement_segment='adapter_continuous') for v in initial['rows'] if v['status']=='PASS']
   assert len(keep)==12
   actual_skips={(v['mode'],v['rotation']) for v in initial['rows'] if v['status']=='SKIP'}
   for c in manifests[impl]['cases']:
    if not c['adapter_supported']:
     keep.append(dict(status='SKIP',mode=c['name'],rotation=c['rotation'],reason=c['skip_reason'],skip_source='logged' if (c['name'],c['rotation']) in actual_skips else 'planner_after_abort',measurement_segment='adapter_continuous' if (c['name'],c['rotation']) in actual_skips else 'planner_after_abort'))
   cases=sorted((b/'p4-adapter-isolated-cases').iterdir());assert len(cases)==9
   for d in cases:
    cm=json.loads((d/'manifest.json').read_text());case=cm['cases'][0]
    assert len(cm['cases'])==1 and case in manifests[impl]['cases']
    for key in ['board','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','test_sources_sha256','component_sources_sha256','memory_schema']:
     assert cm[key]==manifests[impl][key],('adapter isolated',d,key)
    result=json.loads((d/'results.json').read_text())
    valid=result['complete'] and result['matrix_complete'] and result.get('memory_complete') and not result['errors']
    if valid:
     assert len(result['rows'])==1
     value=dict(result['rows'][0],measurement_segment='adapter_isolated/'+d.name)
    else:
     value=dict(status='FAIL',mode=case['name'],rotation=case['rotation'],measurement_segment='adapter_isolated/'+d.name,failure_errors=result['errors'] or ['Incomplete benchmark; see capture log and completion flags'],memory=failed_memory(d/'live.log'),memory_partial=True)
    keep.append(value)
   results[impl]=dict(complete=True,matrix_complete=True,memory_complete=True,errors=[],rows=keep)
   initial['external_timeout']=timeout
   (out/'p4-adapter-continuous-attempt-errors.json').write_text(json.dumps(initial,indent=2)+'\n')
  else:
   results[impl]=json.loads((p/'results.json').read_text())
  z=results[impl]
  assert z['complete'] and z['matrix_complete'] and z['memory_complete'] and not z['errors'],(board,impl)
  lookup={(v['mode'],v['rotation']):v for v in z['rows']}
  expected={(v['name'],v['rotation']) for v in manifests[impl]['cases']}
  assert set(lookup)==expected and len(lookup)==len(z['rows'])
  for v in z['rows']:
   if v['status']=='PASS':
    assert v['scenes']==16,(board,impl,v)
    for heap in ['internal','psram']:
     m=v['memory'];assert 0<=m['after_run'][heap+'_min_free']<=min(m['after_open'][heap+'_free'],m['after_run'][heap+'_free'])
   raw.append(dict(board=board,implementation=impl,**v))
  checks.append(dict(board=board,implementation=impl,passed=sum(v['status']=='PASS' for v in z['rows']),skipped=sum(v['status']=='SKIP' for v in z['rows']),failed=sum(v['status']=='FAIL' for v in z['rows'])))
 if 'adapter' in results:
  for key in ['board','cases','idf_commit','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','test_sources_sha256','component_sources_sha256','memory_schema']:
   assert manifests['present'][key]==manifests['adapter'][key],(board,key)
 tables={impl:{(v['mode'],v['rotation']):v for v in data['rows']} for impl,data in results.items()}
 for c in manifests['present']['cases']:
  row=dict(board=board,mode=c['name'],rotation=c['rotation'],adapter_supported=c['adapter_supported'],adapter_note=c['skip_reason'])
  for impl in ['present','adapter']:
   value=tables.get(impl,{}).get((c['name'],c['rotation']))
   row[impl+'_measurement_segment'] = value.get('measurement_segment','single_suite') if value else '-'
   row[impl+'_status'] = value['status'] if value else '-'
   row[impl+'_initial_failure_note'] = 'Initial attempt failed with ESP_ERR_INVALID_STATE; statistics from unchanged-source followup' if board=='p4-dsi' and impl=='present' and c['name']=='DOUBLE_DIRECT' and c['rotation']==270 else 'Continuous run failed with ESP_ERR_TIMEOUT; isolated retest shown, continuous stability unresolved' if board=='p4-dsi' and impl=='present' and c['name']=='DOUBLE_PARTIAL' and c['rotation']==90 else ''
   if board=='p4-dsi' and impl=='adapter' and c['name']=='DOUBLE_DIRECT' and c['rotation']==270:
    row[impl+'_initial_failure_note']='Continuous adapter run stalled beyond 204s; isolated retest timed out at 240s; FPS invalid, memory only partial snapshots' if value and value['status']=='FAIL' else 'Continuous adapter run stalled beyond 204s; isolated retest shown; continuous stability unresolved'
   if impl=='present' or c['adapter_supported']:assert value and value['status'] in ['PASS','FAIL'],(board,c,impl)
   for field in ['lvgl_fps','cpu','render','flush','scenes']:
    row[impl+'_'+field]=value.get(field,'-') if value and value['status']=='PASS' else 'FAIL' if value and value['status']=='FAIL' and field=='lvgl_fps' else '-'
   for heap in ['internal','psram']:
    prefix=impl+'_'+heap+'_'
    values={k:'-' for k in ['total_bytes','open_used_bytes','open_free_bytes','open_delta_bytes','run_min_free_bytes','after_close_free_bytes']}
    if value and 'after_open' in value.get('memory',{}):
     m=value['memory'];a=m['after_open'];before=m.get('before_open',{});run=m.get('after_run',{});closed=m.get('after_close',{})
     values=dict(total_bytes=a[heap+'_total'],open_used_bytes=a[heap+'_total']-a[heap+'_free'],open_free_bytes=a[heap+'_free'],open_delta_bytes=before[heap+'_free']-a[heap+'_free'] if before else '-',run_min_free_bytes=run.get(heap+'_min_free','-'),after_close_free_bytes=closed.get(heap+'_free','-'))
    row.update({prefix+k:v for k,v in values.items()})
   for field in ['wait_avg_us','wait_max_us','tx_avg_us','tx_max_us']:
    row[impl+'_'+field]=value.get(field,'-') if value else '-'
  rows.append(row)
 backup=b/board/'backup';m=json.loads((backup/'manifest.json').read_text());restore=json.loads((backup/'restored.json').read_text())
 assert restore['verified_full_flash'] and restore['sha256']==m['sha256']
 events=[json.loads(line) for line in (b/(board+'-events.jsonl')).read_text().splitlines()]
 assert events[-1]['stage']=='RESTORE_VERIFIED', (board,'latest run not yet restored')
assert len(rows)==92
present_passed=sum(v['passed'] for v in checks if v['implementation']=='present')
present_failed=sum(v['failed'] for v in checks if v['implementation']=='present')
assert present_passed+present_failed==92
adapter_passed=sum(v['passed'] for v in checks if v['implementation']=='adapter')
adapter_failed=sum(v['failed'] for v in checks if v['implementation']=='adapter')
assert adapter_passed+adapter_failed==46
(out/'validation.json').write_text(json.dumps({'checks':checks,'present_passed':present_passed,'present_failed':present_failed,'adapter_equivalent_passed':adapter_passed,'adapter_failed':adapter_failed,'adapter_continuous_stall':{'board':'p4-dsi','mode':'DOUBLE_DIRECT','rotation':270,'root_cause':'unresolved'},'all_original_flash_restored_verified':True,'configurations_and_common_source_fingerprints_equal':True,'single_pass_all_suites_passed':False,'adapter_unsupported_combinations':sum(not z['adapter_supported'] for z in rows),'adapter_unsupported_logged':sum(z['status']=='SKIP' and z.get('skip_source','logged')=='logged' for z in raw),'initial_p4_failures':[{'mode':'DOUBLE_DIRECT','rotation':270,'error':'ESP_ERR_INVALID_STATE'},{'mode':'DOUBLE_PARTIAL','rotation':90,'error':'ESP_ERR_TIMEOUT'}],'p4_continuous_run_stability':'unresolved','isolated_retests_tracked':True,'visual_confirmation':'No complete visual confirmation for this matrix; pre-flash S31 tearing observation was on original firmware'},indent=2)+'\n')
(out/'all-results.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n')
(out/'raw-memory-phases.json').write_text(json.dumps(raw,ensure_ascii=False,indent=2)+'\n')
with (out/'all-results-bytes.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
def kb(x):return round(x/1024,2) if isinstance(x,(int,float)) else x
headers=['板卡','模式','角度','present FPS','adapter FPS','present RAM占用 KiB','adapter RAM占用 KiB','present PSRAM占用 KiB','adapter PSRAM占用 KiB','present RAM打开剩余 KiB','adapter RAM打开剩余 KiB','present PSRAM打开剩余 KiB','adapter PSRAM打开剩余 KiB','present RAM最小剩余 KiB','adapter RAM最小剩余 KiB','present PSRAM最小剩余 KiB','adapter PSRAM最小剩余 KiB','备注']
def human(z):
 return [z['board'],z['mode'],z['rotation'],z['present_lvgl_fps'],z['adapter_lvgl_fps'],*[kb(z[impl+'_'+heap+'_'+metric]) for metric in ['open_used_bytes','open_free_bytes','run_min_free_bytes'] for heap in ['internal','psram'] for impl in ['present','adapter']],'; '.join(v for v in [z['adapter_note'],z['present_initial_failure_note'],z['adapter_initial_failure_note']] if v)]
humanrows=[human(z) for z in rows]
with (out/'comparison-KiB.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(headers);w.writerows(humanrows)
notes=[
 'FAIL表示该组合没有完整有效帧率；失败行若有内存数值，仅为已经采集到的对应快照，未采到的阶段留空(-)，不能解释成完整场景验证通过。',
 'P4 adapter DIRECT 270°单组重启复测仍超过240秒未完成，最终标为FAIL；这不是声明不支持，不能用-或旧跑分替代。',
 'P4 adapter连续运行到DIRECT 270°同样卡住，超过204秒无场景结束；保留其12个有效组合，剩余9组合另行逐组重启测试。该异常不能只归因于present，根因未定位。',
 'P4逐组合补测的不同组合各自重启，不能与一次完整连续运行的稳定性结论混用；数值用于对应单一模式性能比较。',
 'P4连续补测DOUBLE_PARTIAL 90°又出现ESP_ERR_TIMEOUT，本组当次数据作废。其后组合按单组合重启补测；表内注明测量分段。单组通过不代表连续创建/销毁显示实例稳定，根因未修复。',
 'P4初轮DOUBLE_DIRECT 270°在场景后段/关闭时出现ESP_ERR_INVALID_STATE，本组首次统计作废。表内DIRECT及后续模式取相同生产/测试源码的分段补测；初轮错误与诊断另存，根因未定位，不能解释为整轮一次通过或问题已修复。',
 f'2026-11-07 全模式四角度：present {present_passed}/92组有效统计、{present_failed}组失败，adapter等价组合{adapter_passed}/46组有效统计、{adapter_failed}组失败，其他组合记 -；每个表内数值取一次有效综合测试，补测历史另列，不作统计显著性判断。',
 '每个组合启动时设置软件旋转，组合之间关闭并重新创建显示实例；本轮不是运行时set_rotation跨轴压力测试。',
 'FPS为LVGL 16个有效场景的平均值（整数），不是物理屏幕刷新率。所有角度使用软件旋转，未用硬件旋转替代不支持的组合。',
 'adapter使用独立LVGL任务，present由benchmark任务pump；表中内存包含实际任务栈和测试环境常驻开销，不能全归为组件元数据。',
 'RAM/PSRAM占用 = 显示打开后对应堆total-free。包括固件其他堆分配，不是单独组件净占用，也不包含链接时静态data/bss/IRAM。单位KiB=1024字节。',
 'RAM=INTERNAL|8BIT，PSRAM=SPIRAM|8BIT，两类不重叠。打开增量、总堆、剩余量及关闭回收数据见字节CSV和原始JSON。',
 '最小剩余统计从本组合open前到benchmark结束、close前，覆盖创建和整个综合场景。SDK按各heap历史低水位求和，不保证同一时刻；不能据此声称准确的瞬时峰值占用。',
 'S3启用PSRAM XIP，可用heap总量会随固件代码大小变化；used heap不包括XIP预留，整机预算还应看剩余量与total_bytes，不只看已用堆。',
 'open_delta为每组合before_open到after_open的堆变化；首次组合可能包含LVGL延迟初始化，不能单凭该列认定组件固定净占用。',
 'RGB/DSI共用板级初始化预分配3块framebuffer；模式的整机堆占用受这项共享预留影响，不是组件最小内存需求。',
 'NONE允许撕裂；RGB NONE在adapter为PARTIAL、present为DIRECT持久画布，同名模式的内部路径不完全相同。',
 'S31只测试TE C1/C2×T1/T2；adapter没有等价compose/tile配置，-不代表adapter完全没有TE功能。没有测试关闭TE的S31普通GRAM基线。',
 'AUTO、adapter NONE的软件旋转、S31显式C/T无等价配置记 -，不使用历史结果填充。BOX-3包括internal/PSRAM与tile1/2及AUTO。',
 '原固件均在测试结束后完整恢复并独立校验。串口成功只证明测试完成，不能自动判定屏幕无撕裂；目视异常单独记录。',
 '本轮源码基线25e335214，另补共用测试内存采样及报告解析，生产组件源码不修改；IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe，LVGL9.6.0~1。'
]
md=['# present / adapter 全模式帧率与内存对比','']+['- '+n for n in notes]+['']
for board in boards:
 md+=['## '+board,'','| '+' | '.join(headers[1:9])+' |','|'+ '|'.join(['---']*8)+'|']
 md+=['| '+' | '.join(map(str,human(z)[1:9]))+' |' for z in rows if z['board']==board]
 md+=['']
(out/'README.md').write_text('\n'.join(md)+'\n')
body='<h1>present / adapter 全模式帧率与内存对比</h1><ul>'+''.join('<li>'+html.escape(n)+'</li>' for n in notes)+'</ul>'
for board in boards:
 body+='<h2>'+board+'</h2><div class="scroll"><table><thead><tr>'+''.join('<th>'+html.escape(h)+'</th>' for h in headers[1:])+'</tr></thead><tbody>'
 for z in rows:
  if z['board']==board:body+='<tr>'+''.join('<td>'+html.escape(str(v))+'</td>' for v in human(z)[1:])+'</tr>'
 body+='</tbody></table></div>'
(out/'comparison.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>present adapter 20261107</title><style>body{font:15px system-ui;margin:32px;color:#182333;line-height:1.6}li{margin-bottom:8px}.scroll{overflow:auto}table{border-collapse:collapse;white-space:nowrap;font-variant-numeric:tabular-nums}th,td{padding:8px 12px;border:1px solid #ccd5df;text-align:right}th{background:#e7eef6}tr:nth-child(even){background:#f5f8fb}td:first-child{text-align:left}</style>'+body+'</html>')
# Minimal OOXML workbook: typed cells, frozen headers, filters, no macros or external links.
NS='http://schemas.openxmlformats.org/spreadsheetml/2006/main';REL='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
ET.register_namespace('',NS)
def sheet(data,filtering=True):
 root=ET.Element('{'+NS+'}worksheet')
 views=ET.SubElement(root,'{'+NS+'}sheetViews');view=ET.SubElement(views,'{'+NS+'}sheetView',workbookViewId='0');ET.SubElement(view,'{'+NS+'}pane',xSplit='3',ySplit='1',topLeftCell='D2',activePane='bottomRight',state='frozen')
 cols=ET.SubElement(root,'{'+NS+'}cols');ET.SubElement(cols,'{'+NS+'}col',min='1',max=str(len(data[0])),width='23',customWidth='1')
 sd=ET.SubElement(root,'{'+NS+'}sheetData')
 def colname(n):
  s=''
  while n:n,k=divmod(n-1,26);s=chr(65+k)+s
  return s
 for i,values in enumerate(data,1):
  rr=ET.SubElement(sd,'{'+NS+'}row',r=str(i))
  for j,v in enumerate(values,1):
   cell=ET.SubElement(rr,'{'+NS+'}c',r=f'{colname(j)}{i}')
   if isinstance(v,(int,float)) and not isinstance(v,bool):ET.SubElement(cell,'{'+NS+'}v').text=str(v)
   else:cell.set('t','inlineStr');ET.SubElement(ET.SubElement(cell,'{'+NS+'}is'),'{'+NS+'}t').text=str(v)
 if filtering:ET.SubElement(root,'{'+NS+'}autoFilter',ref=f'A1:{colname(len(data[0]))}{len(data)}')
 return ET.tostring(root,encoding='utf-8',xml_declaration=True)
sheets=[('说明',[['口径说明']]+[[n] for n in notes])]+[(board,[headers]+[human(z) for z in rows if z['board']==board]) for board in boards]+[('原始字节',[list(rows[0])]+[list(z.values()) for z in rows])]
with zipfile.ZipFile(out/'present-adapter-20261107.xlsx','w',zipfile.ZIP_DEFLATED) as z:
 content='<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+''.join(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' for i in range(1,len(sheets)+1))+'</Types>'
 z.writestr('[Content_Types].xml',content)
 z.writestr('_rels/.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="'+REL+'/officeDocument" Target="xl/workbook.xml"/></Relationships>')
 wb=ET.Element('{'+NS+'}workbook');ss=ET.SubElement(wb,'{'+NS+'}sheets')
 for i,(name,data) in enumerate(sheets,1):
  ET.SubElement(ss,'{'+NS+'}sheet',name=name,sheetId=str(i),attrib={'{'+REL+'}id':f'rId{i}'})
  z.writestr(f'xl/worksheets/sheet{i}.xml',sheet(data,i!=1))
 z.writestr('xl/workbook.xml',ET.tostring(wb,encoding='utf-8',xml_declaration=True))
 z.writestr('xl/_rels/workbook.xml.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+''.join(f'<Relationship Id="rId{i}" Type="{REL}/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(sheets)+1))+'</Relationships>')
print('Final report generated:',len(rows),'rows;',out)
