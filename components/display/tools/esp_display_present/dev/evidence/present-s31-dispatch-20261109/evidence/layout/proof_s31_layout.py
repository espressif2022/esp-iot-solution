from pathlib import Path
import json,hashlib,difflib
from elftools.elf.elffile import ELFFile
R=Path('/tmp/present-s31-layout-20261109');code={};cfg={}
for name,impl,key in [('baseline','present','baseline-present'),('baseline','adapter','adapter'),('early','present','early-present')]:
 b=R/name/f's31-rgb/benchmark/{impl}/build';d={}
 for p in sorted((b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir/src').rglob('*.obj')):
  with p.open('rb') as f:
   elf=ELFFile(f);h=hashlib.sha256();n=0
   for s in elf.iter_sections():
    if s.name.startswith('.text') and s.data_size:h.update(s.name.encode());h.update(s.data());n+=s.data_size
   if n:d[str(p.relative_to(b/'esp-idf/lvgl/CMakeFiles/__idf_lvgl.dir'))]=dict(bytes=n,sha256=h.hexdigest())
 code[key]=d
 cfg[key]=dict(l.split('=',1) for l in (b.parent/'sdkconfig').read_text().splitlines() if l.startswith('CONFIG_') and '=' in l)
assert code['baseline-present']==code['adapter']==code['early-present']
assert cfg['baseline-present']==cfg['early-present']
a=R/'baseline/source';b=R/'early/source';diff=[]
for p in sorted(a.rglob('*')):
 if p.is_file() and p.suffix in ['.c','.h','.txt']:
  q=b/p.relative_to(a)
  if q.exists() and p.read_bytes()!=q.read_bytes():diff.append(str(p.relative_to(a)))
expected='components/display/tools/esp_display_present/dev/test_apps/benchmark/CMakeLists.txt';assert diff==[expected],diff
p=a/expected;q=b/expected
(R/'link-only.patch').write_text(''.join(difflib.unified_diff(p.read_text().splitlines(True),q.read_text().splitlines(True),fromfile='a/'+expected,tofile='b/'+expected)))
proof=dict(lvgl_source_objects=len(code['adapter']),all_lvgl_relocatable_text_identical=True,present_sdkconfig_identical=True,present_source_diffs=diff,only_change='two forced-undefined linker symbols',idf_patched=False)
(R/'proof.json').write_text(json.dumps(proof,indent=2));print(json.dumps(proof))
linked={}
for name in ['baseline','early']:
 ep=R/name/'s31-rgb/benchmark/present/build/present_benchmark.elf'
 with ep.open('rb') as f:
  e=ELFFile(f);s=e.get_section_by_name('.symtab');linked[name]={x.name:x['st_size'] for x in s.iter_symbols() if x['st_info']['type']=='STT_FUNC' and x['st_info']['bind'] in ['STB_GLOBAL','STB_WEAK'] and x['st_size'] and isinstance(x['st_shndx'],int) and e.get_section(x['st_shndx']).name in ['.flash.text','.iram0.text']}
a,b=linked.values();proof['linked_function_set_identical']=set(a)==set(b);proof['linked_function_sizes_different']={k:[a.get(k),b.get(k)] for k in sorted(set(a)|set(b)) if a.get(k)!=b.get(k)};proof['linked_function_counts']={k:len(v) for k,v in linked.items()};(R/'proof.json').write_text(json.dumps(proof,indent=2));print('linked size differences',len(proof['linked_function_sizes_different']));print(json.dumps(proof['linked_function_sizes_different'])[:1500])
