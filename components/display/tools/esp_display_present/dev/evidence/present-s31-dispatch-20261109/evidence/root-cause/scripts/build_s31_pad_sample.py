from pathlib import Path
from elftools.elf.elffile import ELFFile
import shutil,subprocess,sys,json
R=Path('/tmp/present-s31-stable-20261109');r=R/'padsample';old=Path('/tmp/present-s31-layout-20261109/baseline');shutil.copytree(R/'pad24/source',r/'source',dirs_exist_ok=True);
if not (r/'deps').exists():(r/'deps').symlink_to((R/'pad24/deps').resolve(),target_is_directory=True)
m=r/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main';p=m/'CMakeLists.txt';p.write_text(p.read_text().replace('diag.c pad.S','diag.c pad.S stable.c'));shutil.copy2(R/'baseline/source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main/stable.c',m/'stable.c');(m/'pc_address.h').write_text('#define PC_MEASURING_ADDR 0u\n')
p=m.parent/'CMakeLists.txt';p.write_text(p.read_text().replace(str(R/'pad24'),str(r))+'\ntarget_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE \"-Wl,-u,pc_sampler_install\")\n')
p=r/'source/components/display/tools/esp_display_present/dev/tools/run.py';s=p.read_text().replace("stop = ('SUITE_PASS ', 'TE_SUITE_PASS ', 'MATRIX_PASS ', 'MATRIX_DONE ', 'present_v2 smoke passed')","stop = ('PC_DONE',)");p.write_text(s)
def build():
 with (R/'padsample-build.log').open('a') as f:subprocess.run([sys.executable,str(p),'build','--board','s31-rgb','--suite','benchmark','--implementation','present','--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl')],stdout=f,stderr=subprocess.STDOUT,check=True)
def symbols(ep):
 with ep.open('rb') as f:
  e=ELFFile(f);sy=e.get_section_by_name('.symtab');d={x.name:(x['st_value'],x['st_size']) for x in sy.iter_symbols() if x.name};fonts=sorted((x.name,x['st_value'],x['st_size']) for x in sy.iter_symbols() if x['st_info']['type']=='STT_OBJECT' and x.name in ['glyph_bitmap','glyph_dsc','cmaps','font_dsc']);return d,fonts
build();ep=r/'s31-rgb/benchmark/present/build/present_benchmark.elf'
for i in range(4):
 target=symbols(ep)[0]['measuring'][0];(m/'pc_address.h').write_text(f'#define PC_MEASURING_ADDR 0x{target:08x}u\n');build()
 if symbols(ep)[0]['measuring'][0]==target:break
else:raise RuntimeError('Non-convergent watch address')
a,af=symbols(R/'pad24/s31-rgb/benchmark/present/build/present_benchmark.elf');b,bf=symbols(ep);assert af==bf
names=['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend','lv_font_get_glyph_dsc_fmt_txt','lv_text_get_next_line','lv_text_get_next_line.part.0'];assert all(a[n]==b[n] for n in names)
backup=r/'s31-rgb/backup';backup.mkdir(exist_ok=True,parents=True)
for n in ['original.bin','manifest.json']:shutil.copy2(R/'baseline/s31-rgb/backup'/n,backup/n)
(r/'s31-rgb/benchmark/present/pc_address.h').write_text((m/'pc_address.h').read_text())
(R/'padsample-ready.json').write_text(json.dumps(dict(ready=True,font_object_addresses_equal=True,hot_function_addresses_equal=True,watch_address=hex(target),functions={n:b[n] for n in names}),indent=2))
print('Pad24 sampler ready with identical function and font addresses',flush=True)
