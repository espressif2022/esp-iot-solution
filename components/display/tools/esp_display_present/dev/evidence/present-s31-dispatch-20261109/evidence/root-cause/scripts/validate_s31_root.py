from pathlib import Path
import json,gzip,re,hashlib,math,subprocess
O=Path('/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-dispatch-20261109');E=O/'evidence/root-cause';data=json.loads((E/'targeted-results.json').read_text());assert len(data)==15
for x in data:
 text=gzip.decompress((E/(x['tag']+'.log.gz')).read_bytes()).decode();assert text.count('PASS mode=')==2 and 'SUITE_PASS ' in text
 assert not any(s in text for s in ['assert failed','Guru Meditation','task_wdt:','ESP_ERROR_CHECK failed'])
 assert len(x['cases'])==2 and x['instruction_conflicts']==0
 for c in x['cases']:
  p=c['probe'];assert p['pixels']==p['render']*384000 and p['flush']==p['render']*10 and p['capacity']==80000 and p['stride']==1600
  assert p['errors']==c['cache']['saturated']==0
  assert all(k in c['memory'] for k in ['before_open','after_open','after_run','after_close'])
pc=json.loads((E/'pc-results.json').read_text());assert len(pc)==4
for x in pc:
 c=x['cases'][0];assert len(c['cores'])==2
 for s in c['cores']:
  assert s['overflow']==0 and s['state']==2
  assert abs(s['samples']-c['probe']['us']/1000)<3
  assert sum(b['count'] for b in c['bins'] if b['core']==s['core'])+s['outside']==s['samples']
assert all(x['constructor_registered'] for x in json.loads((E/'layout-and-pc/constructor-proof.json').read_text()))
assert all(not x['lvgl_global_function_address_changes'] for x in json.loads((E/'layout-and-pc/layout-proof.json').read_text()))
assert len({x['loop_sha256'] for x in json.loads((E/'layout-and-pc/loop-layout-proof.json').read_text())})==1
bounds=json.loads((E/'pc-function-bounds.json').read_text());a=next(x for x in bounds if x['tag']=='sample-present' and x['function']=='lv_font_get_bitmap_fmt_txt');b=next(x for x in bounds if x['tag']=='sample-adapter' and x['function']=='lv_font_get_bitmap_fmt_txt');assert a['lower_ms_per_render']>b['upper_ms_per_render']
proof=json.loads((E/'font-iram/footprint-proof.json').read_text());assert proof['idf_tracked_changes'] is False
assert all(x['iram_delta']==1012 and x['relocatable_text_identical'] and x['lvgl_source_objects']==139 and x['after']['function_section']=='.iram0.text' for x in proof['proof'])
for pair in [('pc-present','pc-adapter'),('aligned-present','aligned-adapter'),('font-present','font-adapter')]:
 m=[json.loads((E/'images'/n/'manifest.json').read_text()) for n in pair];assert m[0]['comparable_sdkconfig_sha256']==m[1]['comparable_sdkconfig_sha256']
for p in (E/'images').iterdir():
 m=json.loads((p/'manifest.json').read_text());assert m['idf_commit']=='7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe'
 assert m['expected_input_bytes']['DOUBLE_PARTIAL/0']==80000
 for name,sha in m['test_sources_sha256'].items():assert hashlib.sha256((p/'test-inputs'/name).read_bytes()).hexdigest()==sha,(p.name,name)
restored=json.loads((E/'restore/restored.json').read_text());original=json.loads((E/'restore/manifest.json').read_text());assert restored['verified_full_flash'] and restored['sha256']==original['sha256']=='5f07493de62e062ad2ffd3b49dd38a2dc66146e67e78257dc0e2500decc36ebd'
for p in O.rglob('*'):
 if p.is_file():assert p.suffix not in ['.elf','.bin','.o','.a'] and 'build' not in p.relative_to(O).parts,p
r={'passed':True,'targeted_cases':30,'pc_images':4,'source_images':11,'counter_and_input_verified':True,'pc_sampling_and_bounds_verified':True,'same_idf_and_comparable_configuration':True,'original_flash_restored':True,'no_build_or_firmware':True};(O/'root-validation.json').write_text(json.dumps(r,indent=2));print(r)
