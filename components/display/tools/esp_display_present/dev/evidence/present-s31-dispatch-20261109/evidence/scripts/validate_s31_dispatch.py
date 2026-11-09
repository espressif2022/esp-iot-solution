from pathlib import Path
import csv,json,hashlib,gzip,math,sys
from parse_s31_dispatch import parse
O=Path(sys.argv[1]);R=Path('/tmp/present-s31-dispatch-20261109');summary=json.loads((O/'summary.json').read_text());raw=json.loads((O/'raw-results.json').read_text());assert len(summary)==192 and len(raw)==4
cases=0
for v in ['off','on']:
 for impl in ['present','adapter']:
  p=R/v/f's31-rgb/benchmark/{impl}';parsed=parse(p/'live.log',v,impl,True);cases+=len(parsed['cases']);dst=O/'evidence'/v/impl
  assert json.loads((dst/'diagnostic-results.json').read_text())==json.loads(json.dumps(parsed))
  assert gzip.decompress((dst/'live.log.gz').read_bytes())==(p/'live.log').read_bytes()
  manifest=json.loads((dst/'manifest.json').read_text())
  for name,sha in manifest['test_sources_sha256'].items():assert hashlib.sha256((O/'evidence/test-inputs'/name).read_bytes()).hexdigest()==sha,name
for x in raw:
 for c in x['cases']:
  for s in c['scenes'].values():
   if c['implementation']=='present':assert sum(t['name']=='present_service' for t in s['tasks'])==2,(c['execution'],s['name'])
   assert len([k for k in s['cache'] if k!='scene' and k!='saturated'])==16
   if s['name']=='Screen sized text':assert s['pixels_per_render']==384000 and s['flushes_per_render']==(10 if c['rotation']==0 else 16)
assert cases==24
proof=json.loads((O/'lvgl-code-proof.json').read_text());assert all(p['identical_text'] and p['lvgl_source_objects']==139 for p in proof.values())
for v in ['off','on']:
 p=json.loads((O/'evidence'/v/'present/manifest.json').read_text());a=json.loads((O/'evidence'/v/'adapter/manifest.json').read_text())
 assert p['idf_commit']==a['idf_commit']=='7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe'
 assert p['comparable_sdkconfig_sha256']==a['comparable_sdkconfig_sha256']
 assert p['test_sources_sha256']==a['test_sources_sha256']
 assert [k for k in p['component_sources_sha256'] if p['component_sources_sha256'][k]!=a['component_sources_sha256'][k]]==['esp_lvgl_adapter/src/display/display_manager.c']
for x in raw:
 for c in x['cases']:
  for s in c['scenes'].values():assert s['execute']['calls']==s['execute']['core0']+s['execute']['core1']==s['execute']['hw']+s['execute']['cpu']

with (O/'scenes.csv').open(encoding='utf-8-sig') as f:rows=list(csv.DictReader(f))
assert len(rows)==384
keys={(x['xip'],x['execution'],int(x['rotation']),int(x['pass_id']),x['name']) for x in rows};assert len(keys)==384
for d in json.loads((O/'decomposition.json').read_text()):assert math.isclose(d['stall_factor'],d['fill_factor']*d['stall_per_fill_factor'],rel_tol=1e-12)
restored=json.loads((O/'evidence/restore/restored.json').read_text());original=json.loads((O/'evidence/restore/manifest.json').read_text());assert restored['verified_full_flash'] and restored['sha256']==original['sha256']
for p in O.rglob('*'):
 if p.is_file():assert p.suffix not in ['.bin','.elf','.o','.a'] and 'build' not in p.relative_to(O).parts,p
(O/'file-validation.json').write_text(json.dumps(dict(passed=True,cases=24,scene_records=384,groups=192,worker_resources_retained=True,counters_per_scene=16,restored=True,no_build_or_firmware=True),indent=2)+'\n')
print('Validated 24 cases, 384 scenes, 192 groups, counter and restoration evidence')
