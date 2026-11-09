from pathlib import Path
from elftools.elf.elffile import ELFFile
import json,time
R=Path('/tmp/present-s31-font-iram-20261109');old=Path('/tmp/present-s31-layout-20261109')
while not (R/'build-completion.json').exists():time.sleep(3)
assert all(x[2]==0 for x in json.loads((R/'build-completion.json').read_text()))
proof=[]
for n,i in [('baseline','present'),('baseline','adapter'),('pad24','present')]:
 ep=R/n/f's31-rgb/benchmark/{i}/build/present_benchmark.elf'
 with ep.open('rb') as f:
  e=ELFFile(f);sy=e.get_section_by_name('.symtab');s=sy.get_symbol_by_name('lv_font_get_bitmap_fmt_txt')[0];assert e.get_section(s['st_shndx']).name=='.iram0.text';proof.append(dict(variant=n,implementation=i,address=hex(s['st_value']),function_bytes=s['st_size'],iram_text_bytes=e.get_section_by_name('.iram0.text').data_size,flash_text_bytes=e.get_section_by_name('.flash.text').data_size))
assert proof[0]['address']==proof[2]['address']
(R/'ready-builds.json').write_text(json.dumps(dict(ready=True,same_function_address_under_pad24=True,proof=proof),indent=2));print('Font bitmap function verified in IRAM for all images',flush=True)
