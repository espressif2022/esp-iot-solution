from pathlib import Path
from decimal import Decimal,ROUND_HALF_UP
import json,zipfile,xml.etree.ElementTree as ET
NS='http://schemas.openxmlformats.org/spreadsheetml/2006/main';REL='http://schemas.openxmlformats.org/officeDocument/2006/relationships'
def column(n):
 s=''
 while n:n,j=divmod(n-1,26);s=chr(65+j)+s
 return s

def sheet(data):
 root=ET.Element('{'+NS+'}worksheet');sd=ET.SubElement(root,'{'+NS+'}sheetData')
 for i,vals in enumerate(data,1):
  row=ET.SubElement(sd,'{'+NS+'}row',r=str(i))
  for j,v in enumerate(vals,1):
   cell=ET.SubElement(row,'{'+NS+'}c',r=f'{column(j)}{i}')
   if isinstance(v,(int,float)) and not isinstance(v,bool):ET.SubElement(cell,'{'+NS+'}v').text=str(v)
   else:cell.set('t','inlineStr');ET.SubElement(ET.SubElement(cell,'{'+NS+'}is'),'{'+NS+'}t').text=str(v)
 return ET.tostring(root,encoding='utf-8',xml_declaration=True)

def write_xlsx(output,rows):
 sheets=[('说明',[['口径'],['FULL/PARTIAL为本轮同容量重测；DIRECT/AUTO/TE/GRAM_T2为历史。'],['LVGL FPS可能包含空刷新；非空绘制也不是实际扫描完成。'],['内存剩余越多越好；源容量仅单张活跃输入，额外像素存储须另计。']])]
 for board in ['s3-rgb','s31-rgb','p4-dsi','s31-qspi','box3-spi']:
  for variant in (['present','present_off'] if board=='s3-rgb' else ['present']):
   data=[['模式','角度','来源','present FPS','adapter FPS','present 绘制/s','adapter 绘制/s','present 源bytes','adapter 源bytes','阶段','present 内部剩余KiB','adapter 内部剩余KiB','present PSRAM剩余KiB','adapter PSRAM剩余KiB']]
   for r in rows:
    if r['board']!=board:continue
    for phase in ['before_open','after_open','after_run','after_close']:
     vals=[r.get(i+'_'+heap+'_'+phase+'_free_bytes','-') for heap in ['internal','psram'] for i in [variant,'adapter']]
     vals=[float((Decimal(v)/1024).quantize(Decimal('.01'),rounding=ROUND_HALF_UP)) if isinstance(v,int) else v for v in vals]
     data.append([r['mode'],r['rotation'],r[variant+'_cohort'],r[variant+'_lvgl_fps'],r['adapter_lvgl_fps'],r.get(variant+'_effective_fps','-'),r.get('adapter_effective_fps','-'),r.get(variant+'_buffer_active_bytes','-'),r.get('adapter_buffer_active_bytes','-'),phase,*vals])
   sheets.append((board+('-off' if variant=='present_off' else ''),data))
 fields=list(dict.fromkeys(k for r in rows for k in r));sheets.append(('RawBytes',[fields]+[[r.get(k,'-') for k in fields] for r in rows]))
 with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
  z.writestr('[Content_Types].xml','<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+''.join(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' for i in range(1,len(sheets)+1))+'</Types>')
  z.writestr('_rels/.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="'+REL+'/officeDocument" Target="xl/workbook.xml"/></Relationships>')
  wb=ET.Element('{'+NS+'}workbook');ss=ET.SubElement(wb,'{'+NS+'}sheets')
  for i,(name,data) in enumerate(sheets,1):ET.SubElement(ss,'{'+NS+'}sheet',name=name,sheetId=str(i),attrib={'{'+REL+'}id':f'rId{i}'});z.writestr(f'xl/worksheets/sheet{i}.xml',sheet(data))
  z.writestr('xl/workbook.xml',ET.tostring(wb,encoding='utf-8',xml_declaration=True));z.writestr('xl/_rels/workbook.xml.rels','<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+''.join(f'<Relationship Id="rId{i}" Type="{REL}/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(sheets)+1))+'</Relationships>')
if __name__=='__main__':
 import sys
 d=Path(sys.argv[1]);write_xlsx(d/'comparison-current.xlsx',json.loads((d/'comparison-current.json').read_text()));print('Workbook generated',d)
