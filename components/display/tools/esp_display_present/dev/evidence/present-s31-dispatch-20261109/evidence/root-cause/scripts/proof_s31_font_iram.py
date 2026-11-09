from pathlib import Path
from elftools.elf.elffile import ELFFile
import hashlib,json,subprocess
R=Path('/tmp/present-s31-font-iram-20261109');old=Path('/tmp/present-s31-layout-20261109');out=[]
for impl in ['present','adapter']:
 paths=[old/'baseline'/f's31-rgb/benchmark/{impl}/build',R/'baseline'/f's31-rgb/benchmark/{impl}/build'];code=[];sizes=[]
 for b in paths:
  objects={}
  for p in (b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir/src').rglob('*.obj'):
   with p.open('rb') as f:
    e=ELFFile(f);h=hashlib.sha256();n=0
    for s in e.iter_sections():
     if s.name.startswith('.text') and s.data_size:h.update(s.name.encode());h.update(s.data());n+=s.data_size
    if n:objects[str(p.relative_to(b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir'))]=h.hexdigest()
  code.append(objects)
  with (b/'present_benchmark.elf').open('rb') as f:
   e=ELFFile(f);s=e.get_section_by_name('.symtab').get_symbol_by_name('lv_font_get_bitmap_fmt_txt')[0];sizes.append(dict(iram=e.get_section_by_name('.iram0.text').data_size,flash=e.get_section_by_name('.flash.text').data_size,function_bytes=s['st_size'],function_section=e.get_section(s['st_shndx']).name))
 assert code[0]==code[1] and len(code[0])==139
 out.append(dict(implementation=impl,lvgl_source_objects=139,relocatable_text_identical=True,before=sizes[0],after=sizes[1],iram_delta=sizes[1]['iram']-sizes[0]['iram']))
print('Measured IRAM deltas:',[(x['implementation'],x['iram_delta']) for x in out]);assert all(1000<=x['iram_delta']<=1040 for x in out)
idf=Path('/home/xuxin/esp_work/esp-idf-qspi');dirty=subprocess.check_output(['git','-C',str(idf),'status','--porcelain','--untracked-files=no'],text=True);assert not dirty.strip()
(R/'footprint-proof.json').write_text(json.dumps(dict(proof=out,idf_tracked_changes=False,idf_commit=subprocess.check_output(['git','-C',str(idf),'rev-parse','HEAD'],text=True).strip()),indent=2));print(json.dumps(out,indent=2))
