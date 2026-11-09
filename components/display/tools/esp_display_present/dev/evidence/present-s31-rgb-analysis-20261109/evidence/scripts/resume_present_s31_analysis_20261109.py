from pathlib import Path
import json,time,traceback,shutil,hashlib
R=Path('/tmp/present-s31-rgb-analysis-20261109')
while True:
 try:s=json.loads((R/'stage.json').read_text())
 except (FileNotFoundError,json.JSONDecodeError):s={}
 if s.get('action')=='done':break
 time.sleep(5)
assert s['restored'] and not s['tests_succeeded'],s
(R/'first-controller-final.json').write_text(json.dumps(s,indent=2)+'\n')
for v in ['baseline','burst_cache']:
 assert json.loads((R/v/'prebuild-control-result.json').read_text())['exit_code']==0
for impl in ['present','adapter']:assert json.loads((R/f'xip/prebuild-{impl}-result.json').read_text())['exit_code']==0
assert 'Build passed:' in (R/'adapter_matched/prebuild.log').read_text()
partial=json.loads((R/'tile2/s31-rgb/benchmark/present-profile/results.json').read_text());assert not partial['complete'] and len(partial['rows'])==2 and any('ESP_ERR_NO_MEM' in e for e in partial['errors'])
source=Path('/tmp/run_present_s31_analysis_20261109.py').read_text();exec(compile(source.split('changed=False;success=False;restored=False')[0],'<s31-controller-functions>','exec'))
changed=False;success=False;restored=False
try:
 stage('resume_after_tile2_allocation_failure',tile2_completed_cases=2,tile2_failed_mode='TRIPLE_PARTIAL 0deg')
 for v in ['baseline','burst_cache']:test(v,profile=False,build=False)
 for impl in ['present','adapter']:test('xip',impl=impl,profile=False,build=False)
 test('adapter_matched',impl='adapter',profile=False,build=False)
 success=True
except BaseException as e:
 stage('resumed_failed',error=repr(e));(R/'resume-failure.txt').write_text(traceback.format_exc())
finally:
 if changed:
  try:
   run('baseline','restore');b=R/'baseline/s31-rgb/backup'
   for name in ['restore.log','restore-verify.log','restored.json']:shutil.copy2(b/name,PERSIST/name)
   assert json.loads((b/'restored.json').read_text())['verified_full_flash'];restored=True
  except BaseException as e:stage('restore_failed',error=repr(e));(R/'resume-restore-failure.txt').write_text(traceback.format_exc())
 stage('done',tests_succeeded=success,firmware_changed=changed,restored=restored,xip_complete=success,adapter_matched_complete=success,tile2_matrix_failed=True)
raise SystemExit(0 if success and restored else 1)
