from pathlib import Path
import json,re,gzip,hashlib
R=Path('/tmp/present-s31-layout-20261109');out=[]
for tag in ['base-before','adapter','early','base-after']:
 s=(R/f'{tag}.log').read_text();assert 'SUITE_PASS ' in s and s.count('PASS mode=')==2
 assert not any(x in s for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed'])
 rows=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line)) for line in s.splitlines() if 'DEEP_PROBE: scene=0 ' in line];assert len(rows)==2
 cache=[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line)) for line in s.splitlines() if 'DEEP_CACHE: scene=0 ' in line];assert len(cache)==2
 for r,c in zip(rows,cache):
  assert r['render']>20 and r['pixels']==r['render']*384000 and r['flush']==r['render']*10 and r['capacity']==80000 and r['stride']==1600
  assert r['errors']==0 and c['saturated']==0 and c['ic0']==c['ic1']==0
  assert r['us']>15_000_000
 out.append(dict(tag=tag,render_counts=[r['render'] for r in rows],measured_us=[r['us'] for r in rows]))
proof=json.loads((R/'proof.json').read_text());assert proof['lvgl_source_objects']==139 and proof['all_lvgl_relocatable_text_identical'] and proof['present_sdkconfig_identical'] and proof['linked_function_set_identical']
a=json.loads((R/'baseline/s31-rgb/benchmark/present/manifest.json').read_text());b=json.loads((R/'baseline/s31-rgb/benchmark/adapter/manifest.json').read_text());e=json.loads((R/'early/s31-rgb/benchmark/present/manifest.json').read_text());assert a['comparable_sdkconfig_sha256']==b['comparable_sdkconfig_sha256']==e['comparable_sdkconfig_sha256'];assert a['component_sources_sha256']==e['component_sources_sha256']
restore=json.loads((R/'baseline/s31-rgb/backup/restored.json').read_text());orig=json.loads((R/'baseline/s31-rgb/backup/manifest.json').read_text());assert restore['verified_full_flash'] and restore['sha256']==orig['sha256']
assert json.loads((R/'completion.json').read_text())['success']
(R/'validation.json').write_text(json.dumps(dict(passed=True,case_count=8,runs=out,restored=True,common_configuration=True,same_present_runtime=True),indent=2));print('Validated 8 full-screen text runs; sources, input capacity, counters and full restoration match')
