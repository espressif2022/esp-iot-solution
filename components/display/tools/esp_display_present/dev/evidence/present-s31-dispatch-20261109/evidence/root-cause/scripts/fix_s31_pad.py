from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,subprocess,sys,os,signal,time,shutil,traceback
R=Path('/tmp/present-s31-stable-20261109');r=R/'pad24';old=Path('/tmp/present-s31-layout-20261109/baseline');pid=json.loads((R/'paused-controller.json').read_text())['pid'];result={'success':False}
try:
 src=r/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/CMakeLists.txt';s=src.read_text();anchor='include($ENV{IDF_PATH}/tools/cmake/project.cmake)';line=f'add_compile_options("-fmacro-prefix-map={r}={old}")\n';assert anchor in s;src.write_text(s.replace(anchor,line+anchor,1))
 runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py';base=[sys.executable,str(runner)]
 with (R/'pad24-normalized-build.log').open('w') as f:subprocess.run(base+['build','--board','s31-rgb','--suite','benchmark','--implementation','present','--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl')],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=360)
 def objects(path):
  with path.open('rb') as f:
   e=ELFFile(f);sy=e.get_section_by_name('.symtab');names=['lv_font_montserrat_20','lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend','lv_font_get_glyph_dsc_fmt_txt','lv_text_get_next_line'];d={n:(sy.get_symbol_by_name(n)[0]['st_value'],sy.get_symbol_by_name(n)[0]['st_size']) for n in names};d['font_objects']=sorted((x.name,x['st_value'],x['st_size']) for x in sy.iter_symbols() if x['st_info']['type']=='STT_OBJECT' and x.name in ['glyph_bitmap','glyph_dsc','cmaps','font_dsc']);return d
 a=objects(old/'s31-rgb/benchmark/present/build/present_benchmark.elf');b=objects(r/'s31-rgb/benchmark/present/build/present_benchmark.elf');assert a['font_objects']==b['font_objects'];assert a['lv_font_montserrat_20']==b['lv_font_montserrat_20']
 for n in ['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend','lv_font_get_glyph_dsc_fmt_txt','lv_text_get_next_line']:assert b[n][0]-a[n][0]==24 and b[n][1]==a[n][1],(n,a[n],b[n])
 result.update(font_objects_identical_addresses=True,font_object_count=len(a['font_objects']),code_shift_bytes=24,before=a,after=b)
 # The paused parent has an active capture child. Wait for it to release USB0.
 deadline=time.monotonic()+90
 while True:
  active=False
  for p in Path('/proc').glob('[0-9]*/cmdline'):
   try:argv=p.read_bytes().split(b'\0')
   except (FileNotFoundError,PermissionError):continue
   if str(runner).encode() in argv and b'capture' in argv:active=True
  if not active:break
  if time.monotonic()>deadline:raise RuntimeError('Original capture child did not finish')
  time.sleep(1)
 shutil.copy2(r/'s31-rgb/benchmark/present/live.log',R/'pad24-before-normalization.log')
 for action in ['flash','capture']:
  with (R/f'pad24-normalized-{action}.log').open('w') as f:subprocess.run(base+[action,'--board','s31-rgb','--suite','benchmark','--implementation','present','--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl'),'--port','/dev/ttyUSB0','--timeout','150'],check=True,stdout=f,stderr=subprocess.STDOUT,timeout=240)
 text=(r/'s31-rgb/benchmark/present/live.log').read_text();assert 'SUITE_PASS ' in text and text.count('PASS mode=')==2;assert not any(s in text for s in ['Guru Meditation','assert failed','task_wdt:'])
 result['success']=True
except BaseException:result['error']=traceback.format_exc();print(result['error'],flush=True)
finally:
 (R/'pad-normalization.json').write_text(json.dumps(result,indent=2));os.kill(pid,signal.SIGCONT);print('Resumed original test controller; normalization success:',result['success'],flush=True)
assert result['success']
