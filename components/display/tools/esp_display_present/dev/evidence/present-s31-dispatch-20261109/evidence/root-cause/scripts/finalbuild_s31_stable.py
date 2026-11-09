from pathlib import Path
from elftools.elf.elffile import ELFFile
import subprocess,sys,time,json,hashlib
R=Path('/tmp/present-s31-stable-20261109');old=Path('/tmp/present-s31-layout-20261109')
while not (R/'build-completion.json').exists():time.sleep(3)
assert all(x[2]==0 for x in json.loads((R/'build-completion.json').read_text()))
def symbols(path):
 with path.open('rb') as f:
  e=ELFFile(f);return {s.name:(s['st_value'],s['st_size']) for s in e.get_section_by_name('.symtab').iter_symbols() if s.name and (s.name=='measuring' or s['st_info']['type']=='STT_FUNC' and s['st_info']['bind'] in ['STB_GLOBAL','STB_WEAK'])}
def build(name,impl):
 r=R/name;runner=r/'source/components/display/tools/esp_display_present/dev/tools/run.py'
 with (r/f'final-build-{impl}.log').open('a') as f:subprocess.run([sys.executable,str(runner),'build','--board','s31-rgb','--suite','benchmark','--implementation',impl,'--profiles','DOUBLE_PARTIAL','--angles','0','--work-root',str(r),'--lvgl-dir',str(R/'lvgl')],check=True,stdout=f,stderr=subprocess.STDOUT)
proof=[]
for name,impl in [('baseline','present'),('baseline','adapter'),('early','present')]:
 ep=R/name/f's31-rgb/benchmark/{impl}/build/present_benchmark.elf';header=R/name/'source/components/display/tools/esp_display_present/dev/test_apps/benchmark/main/pc_address.h'
 # Both implementations share source/header but each has its own measured flag address.
 # Final header may differ between builds; archive the exact value per image.
 for attempt in range(4):
  target=symbols(ep)['measuring'][0];header.write_text(f'#define PC_MEASURING_ADDR 0x{target:08x}u\n');build(name,impl)
  actual=symbols(ep)['measuring'][0]
  if actual==target:break
 else:raise RuntimeError('Measured flag address failed to converge')
 p=ep.parent.parent; (p/'pc_address.h').write_text(header.read_text())
 with ep.open('rb') as f:
  elf=ELFFile(f);symbol=elf.get_section_by_name('.symtab').get_symbol_by_name('pc_sampler_install')[0]
  assert elf.get_section(symbol['st_shndx']).name=='.iram0.text'
  sy=elf.get_section_by_name('.symtab');start=sy.get_symbol_by_name('__init_array_start')[0]['st_value'];end=sy.get_symbol_by_name('__init_array_end')[0]['st_value']
  sec=next(s for s in elf.iter_sections() if s['sh_addr']<=start and end<=s['sh_addr']+s['sh_size'])
  data=sec.data()[start-sec['sh_addr']:end-sec['sh_addr']]
  import struct
  entries=struct.unpack('<'+'I'*(len(data)//4),data);assert symbol['st_value'] in entries

 a=symbols(old/name/f's31-rgb/benchmark/{impl}/build/present_benchmark.elf');b=symbols(ep)
 diffs={k:dict(before=a[k],after=b.get(k)) for k in a if k.startswith('lv_') and a[k]!=b.get(k)}
 proof.append(dict(variant=name,implementation=impl,watch_address=hex(actual),lvgl_global_function_address_changes=diffs,lvgl_global_function_count=sum(k.startswith('lv_') for k in a),sampler_hash=hashlib.sha256((ep.parent.parent/'pc_address.h').read_bytes()).hexdigest()))
 print(name,impl,'LVGL address changes',len(diffs),'watch',hex(actual),flush=True)
(R/'layout-proof.json').write_text(json.dumps(proof,indent=2));assert all(not x['lvgl_global_function_address_changes'] for x in proof), 'Sampler perturbed LVGL layout'
# The plain pad image must shift hot LVGL functions by exactly24 bytes.
a=symbols(old/'baseline/s31-rgb/benchmark/present/build/present_benchmark.elf');b=symbols(R/'pad24/s31-rgb/benchmark/present/build/present_benchmark.elf')
for n in ['lv_draw_sw_blend_color_to_rgb565','lv_draw_sw_blend','lv_font_get_glyph_dsc_fmt_txt','lv_text_get_next_line']:
 assert b[n][0]-a[n][0]==24,(n,a[n],b[n])
(R/'ready-builds.json').write_text(json.dumps(dict(ready=True,unchanged_lvgl_addresses=True,pad24_verified=True,images=4)))
