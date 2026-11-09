from pathlib import Path
from elftools.elf.elffile import ELFFile
import json
R=Path('/tmp/present-s31-stable-20261109');pc=json.loads((R/'pc-results.json').read_text());roots={'sample-present':('baseline','present'),'sample-adapter':('baseline','adapter'),'sample-early':('early','present'),'sample-pad24':('padsample','present')};names=['lv_font_get_bitmap_fmt_txt','lv_text_get_next_line.part.0','lv_font_get_glyph_dsc_fmt_txt','lv_draw_sw_blend_color_to_rgb565'];out=[]
for x in pc:
 n,i=roots[x['tag']]
 with (R/n/f's31-rgb/benchmark/{i}/build/present_benchmark.elf').open('rb') as f:
  e=ELFFile(f);sy=e.get_section_by_name('.symtab')
  for fn in names:
   symbols=sy.get_symbol_by_name(fn);assert len(symbols)==1
   s=symbols[0];a=s['st_value'];b=a+s['st_size'];bins=x['cases'][0]['bins'];nframes=x['cases'][0]['probe']['render'];lower=sum(t['count'] for t in bins if a<=int(t['pc'],16) and int(t['pc'],16)+64<=b);upper=sum(t['count'] for t in bins if int(t['pc'],16)<b and a<int(t['pc'],16)+64)
   out.append(dict(tag=x['tag'],function=fn,lower_samples=lower,upper_samples=upper,lower_ms_per_render=lower/nframes,upper_ms_per_render=upper/nframes))
(R/'pc-function-bounds.json').write_text(json.dumps(out,indent=2))
for x in out:
 if x['function']=='lv_font_get_bitmap_fmt_txt':print(x['tag'],round(x['lower_ms_per_render'],2),round(x['upper_ms_per_render'],2))
