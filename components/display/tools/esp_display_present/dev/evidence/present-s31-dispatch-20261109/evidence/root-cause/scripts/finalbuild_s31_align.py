from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,time,subprocess,sys
R=Path('/tmp/present-s31-align-20261109')
while not (R/'build-completion.json').exists():time.sleep(3)
subprocess.run([sys.executable,'/tmp/build_s31_align.py'],check=True)
proof=[]
for name,impl in [('baseline','present'),('baseline','adapter'),('pad24','present')]:
 ep=R/name/f's31-rgb/benchmark/{impl}/build/present_benchmark.elf'
 with ep.open('rb') as f:
  e=ELFFile(f);sy=e.get_section_by_name('.symtab');names=['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend','lv_font_get_glyph_dsc_fmt_txt','lv_font_get_bitmap_fmt_txt','lv_text_get_next_line.part.0'];a={n:sy.get_symbol_by_name(n)[0]['st_value'] for n in names};assert all(v%32==0 for v in a.values()),a
  proof.append(dict(variant=name,implementation=impl,hot_functions_aligned32=True,addresses={k:hex(v) for k,v in a.items()},flash_text_bytes=e.get_section_by_name('.flash.text').data_size))
(R/'ready-builds.json').write_text(json.dumps(dict(ready=True,compiler_flag='-fmin-function-alignment=32',proof=proof),indent=2));print('Alignment32 verified in all images',flush=True)
