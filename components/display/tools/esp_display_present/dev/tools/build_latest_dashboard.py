# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Assemble the latest recorded five-board report; no hardware operations."""
from pathlib import Path
import argparse,copy,json,gzip,re,hashlib


def load(p):return json.loads(p.read_text())
def scene_scores(p):
 text=gzip.open(p,'rt',errors='replace').read() if p.suffix=='.gz' else p.read_text(errors='replace');scores={}
 for m in re.finditer(r'SCENE mode=(\w+) rotation=(\d+) name="([^"]+)" fps=(\d+)',text):scores.setdefault((m[1],int(m[2])),[]).append({'name':m[3],'fps':int(m[4])})
 return scores

def assemble(evidence):
 base=load(evidence/'rgb-mipi-current-complete-20261111/matrix-data.json');payload=copy.deepcopy(load(evidence/'rgb-mipi-current-complete-20261111/comparison.json'))
 payload.update(schema='display-dashboard-v2',unified=True,updated='2026-11-11',sourceCommit='39156930da8a2c80c95b15f0e159918296556262',currentRound={'label':'五板最新已归档结果'},modes=['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'])
 payload['roundNote']='RGB / MIPI：仅 XIP ON；P4 五项为 39156930d 修复后回归。TE：11-11 带顶部模式标注复测；SPI：最近归档数据，未在本轮重新上板。各项来源随表显示。'
 payload['caution']='本页汇总最新可用记录，不代表五板全部使用同一版本重新测试。P4 的两项未完成地址诊断来自修复前；修复后五项普通回归已通过。TE 原生配置分别列出，不强行映射。串口通过不能证明无脏块或撕裂。'
 payload['methodNote']='RGB/MIPI 不含 NONE，且只使用 XIP ON 数据；S3 开 bounce，S31 RGB 关闭 bounce。P4 五项回归每项完整16场景、正常关闭，未重做驱动地址观察，原始失败现场保留。TE 的 present C/T 组合与 adapter SYNC_FULL / ASYNC_FULL / ASYNC_BOUNCE 分别列出，− 表示不支持、未采集或没有一一对应配置，具体原因见来源说明。TE 新测含固定顶部标签，负载与无标签基线不同，不能据此推导跨实现差值。SPI GRAM 模式允许撕裂；adapter 不支持的软件旋转与 AUTO 记−。各项帧率、内存均来自完整16场景且成功关闭的记录，未补造缺失数据。'
 for b in payload['boards']:
  b.update(modes=payload['modes'],scanout=True,hasDiagnostic=True,configBadge='XIP ON · 防撕裂模式',tableNote='四角度并排 · 来源逐项标注',performanceNote='LVGL 16场景综合分，越高越好。箭头为 present−adapter；AUTO 对 adapter 不适用。')
  b['cohorts']={c:{i:copy.deepcopy(base['boards'][b['id']]['cohorts'][c][i]['rows']) for i in ['present','adapter']} for c in ['clean','diagnostic']}
  for cohort in ['clean','diagnostic']:
   for impl,rows in b['cohorts'][cohort].items():
    for row in rows:
     row['origin']=('11-11 · 完整记录' if impl=='present' else '已核对 adapter 基线') if cohort=='clean' else '11-11 · 独立驱动观察'
     row['condition']='XIP ON; no title overlay' if cohort=='clean' else 'observer overlay; not performance baseline'
     if impl=='adapter':row.pop('transport_fps',None)
  b['originNote']='最近完整采集记录；已测且配置相同的项复用'
 p4=next(b for b in payload['boards'] if b['id']=='p4-dsi');fix=evidence/'p4-rotated-ppa-stall-20261111';scores=scene_scores(fix/'regression/live.log.gz')
 for r in load(fix/'regression/results.json')['rows']:
  row=copy.deepcopy(r);row['scene_scores']=scores[(r['mode'],r['rotation'])];row.update(origin='11-11 · PPA 修复后',source_commit='39156930d',condition='clean IDF; PPA risk requests fall back to CPU')
  rows=p4['cohorts']['clean']['present'];idx=next(i for i,x in enumerate(rows) if (x['mode'],x['rotation'])==(row['mode'],row['rotation']));row['prior_status']=rows[idx]['status'];rows[idx]=row
 for r in p4['cohorts']['clean']['present']:
  if r.get('source_commit')!='39156930d':r['origin']='11-11 · 修复前完整记录'
 for impl in ['present','adapter']:
  for r in p4['cohorts']['diagnostic'][impl]:r['origin']='11-11 · PPA 修复前观察'
 p4.update(originNote='五项已换为PPA修复后结果，其余保留最近完整记录',diagnosticLabel='交接与供数诊断 · PPA 修复前',regressionNote='39156930d：旋转风险尾块在PPA提交前回退CPU。DOUBLE_DIRECT270、DOUBLE_PARTIAL90/270、TRIPLE_PARTIAL90、AUTO270共80场景正常结束；独立重放20000次像素一致，host44/44通过。下方两项未完成诊断是修复前记录，不是本次修复后再次失败；未重新上板的诊断不能当作新版本零异常。')
 old=load(evidence/'five-board-regression-20261109/raw-results.json');aligned=load(evidence/'five-board-regression-20261109/aligned-raw-results.json')
 def enrich(rows,path,origin):
  scores=scene_scores(path);res=copy.deepcopy(rows)
  for r in res:
   r.update(origin=origin,condition='clean IDF; no title overlay')
   if r['status']=='PASS':r['scene_scores']=scores[(r['mode'],r['rotation'])]
  return res
 latest=evidence/'mosaico-te-visible-20261111';teRows=enrich(load(latest/'results-with-te.json')['rows'],latest/'live.log.gz','11-11 · 顶部标注复测')
 for r in teRows:r.update(condition='visual diagnostic with top title; no direct adapter delta',source_commit='39156930d')
 teAdapter=enrich(old['s31-qspi']['adapter']['rows'],evidence/'five-board-regression-20261109/evidence/s31-qspi/adapter/live.log.gz','11-09 · adapter 原生基线')
 for r in teAdapter:r.pop('transport_fps',None)
 tm=['TE_C1_T1','TE_C1_T2','TE_C2_T1','TE_C2_T2','ADAPTER_TE_SYNC_FULL','ADAPTER_TE_ASYNC_FULL','ADAPTER_TE_ASYNC_BOUNCE']
 def rectangular(modes,rows,missing_status='UNPAIRED',reason='另一实现采用独立原生配置；无一一等价项'):
  by={(r['mode'],r['rotation']):r for r in rows}
  return [by.get((m,a),{'mode':m,'rotation':a,'status':missing_status,'reason':reason}) for m in modes for a in [0,90,180,270]]
 def extra(bid,name,resolution,modes,rows,config,note,method):
  return {'id':bid,'name':name,'resolution':resolution,'modes':modes,'scanout':False,'hasDiagnostic':False,'config':config,'configBadge':config,'originNote':note,'frameBytes':0,'memoryAccounting':None,'method':method,'tableNote':'四角度并排 · 未采集／不支持／不等价项记−','cohorts':{'clean':rows,'diagnostic':{i:rectangular(modes,[], 'MISSING','本板未采集地址交接观察') for i in ['present','adapter']}}}
 te=extra('s31-qspi','S31 MOSAICO · TE','480 × 480',tm,{'present':rectangular(tm,teRows),'adapter':rectangular(tm,teAdapter)},'QSPI 40MHz · TE43','present11-11新测；adapter为最近原生配置基线','本轮present16组、256场景，顶部固定显示模式/角度。两次早期USB Hub断连记录已分开；随后完整一轮成功结束。未注入地址交接检测，串口结果不证明画面无脏块。TE等待与传输统计见下表。')
 te.update(visualDiagnostic=True,performanceNote='C=compose buffer数，T=输入tile数。present C/T与adapter原生配置分别列出，无一一等价项，故不计算两者差值。11-11 present带顶部标签，不能与无标签adapter基线作严格性能对照。')
 payload['boards'].append(te)
 spi={}
 for impl in ['present','adapter']:
  rows=enrich(old['box3-spi'][impl]['rows'],evidence/f'five-board-regression-20261109/evidence/box3-spi/{impl}/live.log.gz','11-09 · 最近归档')
  newer=enrich(aligned['box3-spi/'+impl]['rows'],evidence/f'present-aligned-fill-20261109/evidence/box3-spi/{impl}/live.log.gz','11-09 · 单tile容量对齐')
  lookup={(r['mode'],r['rotation']):r for r in newer};rows=[lookup.get((r['mode'],r['rotation']),r) for r in rows]
  if impl=='adapter':
   for r in rows:r.pop('transport_fps',None)
  spi[impl]=rows
 sm=['GRAM_T1_INTERNAL','GRAM_T2_INTERNAL','GRAM_T1_PSRAM','GRAM_T2_PSRAM','AUTO_T2_INTERNAL']
 box=extra('box3-spi','ESP-BOX-3 · SPI','320 × 240',sm,spi,'SPI GRAM · 允许撕裂','最近归档11-09，本次仅更新网页，未重新上板','SPI GRAM没有RGB/MIPI内部扫描帧缓冲，本表传输路径允许撕裂，未采集地址观察。adapter只在0°支持这些GRAM输入配置，不支持的软件旋转和AUTO记−。')
 box['performanceNote']='LVGL完整16场景综合分。单tile行容量双方已对齐；双tile/AUTO使用最近归档记录。SPI GRAM允许撕裂，adapter不支持的软件旋转与AUTO记−。'
 payload['boards'].append(box)
 return payload


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--evidence-root',type=Path,default=Path(__file__).resolve().parents[1]/'evidence');p.add_argument('--accounting',type=Path,help='Archived whole-image SRAM accounting; never estimated');p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=assemble(a.evidence_root)
 if a.accounting:
  for board in out['boards']:
   item=load(a.accounting)['boards'].get(board['id'])
   if item:board.update(memoryAccounting=item['accounts'],accountingNote=item['note'])
 else:
  next(b for b in out['boards'] if b['id']=='p4-dsi')['accountingNote']='静态账目取原矩阵归档固件；本次PPA回归的ELF统计需单独提供。'
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
 print('assembled',a.output)
if __name__=='__main__':main()
