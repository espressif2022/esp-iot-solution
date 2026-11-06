from pathlib import Path
import json,re,hashlib,sys
b=Path(sys.argv[1]) if len(sys.argv)>1 else Path('/tmp/present-refactor-hardware-20261106');checks=[];matrix=[]
keys=['board','cases','idf_commit','lvgl_version','lvgl_benchmark_sha256','comparable_sdkconfig_sha256','defaults_sha256','test_sources_sha256']
for board in ['s3-rgb','p4-dsi']:
 for suite in ['pixels','benchmark']:
  manifests={v:json.loads((b/v/board/suite/'present/manifest.json').read_text()) for v in ['baseline','current']}
  for key in keys:assert manifests['baseline'][key]==manifests['current'][key],(board,suite,key)
  for variant,m in manifests.items():
   components=b/(variant+'-tree')/'components/display/tools'
   for p,expected in m['component_sources_sha256'].items():assert hashlib.sha256((components/p).read_bytes()).hexdigest()==expected,(variant,p)
   dev=components/'esp_display_present/dev'
   for p,expected in m['test_sources_sha256'].items():assert hashlib.sha256((dev/p).read_bytes()).hexdigest()==expected,(variant,p)
  checks.append({'board':board,'suite':suite,'all_common_keys_equal':True,'runtime_and_test_source_hashes_match':True})
  if suite=='pixels':
   for variant in ['baseline','current']:
    root=b/variant/board/suite/'present';result=json.loads((root/'results.json').read_text());assert result['complete'] and not result['errors']
    rows=re.findall(r'CASE_PASS board=.*? profile=(\S+) rotation=(\d+) frames=(\d+) avg_frame_us=(\d+) pixels=ALL', (root/'live.log').read_text())
    assert len(rows)==28 and len({(r[0],r[1]) for r in rows})==28 and all(r[2]=='4' for r in rows)
    matrix.append({'board':board,'variant':variant,'cases_passed':28,'frames_per_case':4,'rows':[{'profile':p,'rotation':int(a),'avg_frame_us':int(t)} for p,a,n,t in rows]})
(b/'comparability.json').write_text(json.dumps(checks,indent=2)+'\n');(b/'pixel-matrix-results.json').write_text(json.dumps(matrix,indent=2)+'\n')
print('Matching config/test fingerprints and final source hashes on both boards; pixel matrix 112/112')
