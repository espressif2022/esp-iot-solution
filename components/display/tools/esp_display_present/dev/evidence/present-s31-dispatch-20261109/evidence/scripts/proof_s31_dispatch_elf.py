from pathlib import Path
import hashlib,json,glob
from elftools.elf.elffile import ELFFile
R=Path('/tmp/present-s31-dispatch-20261109');result={};placement=[]
for v in ['off','on']:
 paths={impl:R/v/f's31-rgb/benchmark/{impl}/build' for impl in ['present','adapter']}
 code={}
 for impl,b in paths.items():
  d={}
  for p in sorted((b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir/src').rglob('*.obj')):
   with p.open('rb') as f:
    elf=ELFFile(f);h=hashlib.sha256();n=0
    for s in elf.iter_sections():
     if s.name.startswith('.text') and s.data_size:
      h.update(s.name.encode());h.update(s.data());n+=s.data_size
   if n:d[str(p.relative_to(b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir'))]=dict(bytes=n,sha256=h.hexdigest())
  code[impl]=d
  app=json.loads((b/'flasher_args.json').read_text())['app']['file'];ep=b/Path(app).with_suffix('.elf')
  if not ep.exists():ep=next(b.glob('*.elf'))
  with ep.open('rb') as f:
   elf=ELFFile(f);sym=elf.get_section_by_name('.symtab')
   names=['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend_image_to_rgb565','lv_draw_sw_blend','lv_draw_sw_letter','lv_draw_sw_fill','esp_display_present_engine_acquire_buffer','esp_display_present_executor_run','bench_diag_inline','rotate_copy_strided_region']
   for name in names:
    matches=sym.get_symbol_by_name(name) or []
    for entry in matches:
     idx=entry['st_shndx'];section=elf.get_section(idx).name if isinstance(idx,int) else str(idx)
     placement.append(dict(xip=v,implementation=impl,function=name,address=hex(entry['st_value']),bytes=entry['st_size'],section=section,line_offset_64=entry['st_value']%64))
 assert set(code['present'])==set(code['adapter'])
 diffs=[k for k in code['present'] if code['present'][k]!=code['adapter'][k]]
 result[v]=dict(lvgl_source_objects=len(code['present']),identical_text=not diffs,differing_objects=diffs,objects=code['present'])
(R/'lvgl-code-proof.json').write_text(json.dumps(result,indent=2)+'\n');(R/'code-placement.json').write_text(json.dumps(placement,indent=2)+'\n')
for v,d in result.items():print(v,d['lvgl_source_objects'],'LVGL source objects, identical text:',d['identical_text'],'diffs',d['differing_objects'])
print(json.dumps([p for p in placement if p['function'] in ['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend_image_to_rgb565']],indent=2))
