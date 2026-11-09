from pathlib import Path
import re,subprocess,json,hashlib
from elftools.elf.elffile import ELFFile
R=Path('/tmp/present-s31-stable-20261109');old=Path('/tmp/present-s31-layout-20261109');obj='/home/xuxin/.espressif/tools/riscv32-esp-elf/esp-16.1.0_20260609/riscv32-esp-elf/bin/riscv32-esp-elf-objdump';out=[]
for label,root,name,impl in [('present-original',old,'baseline','present'),('adapter',old,'baseline','adapter'),('present-early',old,'early','present'),('present-pad24',R,'pad24','present')]:
 ep=root/name/f's31-rgb/benchmark/{impl}/build/present_benchmark.elf';asm=subprocess.check_output([obj,'-d','--disassemble=lv_font_get_bitmap_fmt_txt',str(ep)],text=True);(R/f'{label}-font-bitmap.asm').write_text(asm)
 matches=re.findall(r'^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+blt\s+a4,a1,([0-9a-f]+)',asm,re.M);assert len(matches)==1,matches
 branch,inst,target=matches[0];start=int(target,16);end=int(branch,16)+len(inst)//2
 with ep.open('rb') as f:
  e=ELFFile(f);sec=e.get_section_by_name('.flash.text');payload=sec.data()[start-sec['sh_addr']:end-sec['sh_addr']]
 out.append(dict(implementation=label,loop_start=hex(start),loop_end_exclusive=hex(end),bytes=end-start,offset_in_cache_line=start%64,cache_lines=(end-1)//64-start//64+1,loop_sha256=hashlib.sha256(payload).hexdigest()))
(R/'loop-layout-proof.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
