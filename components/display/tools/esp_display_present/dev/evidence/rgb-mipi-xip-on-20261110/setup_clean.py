from pathlib import Path
import shutil,json,hashlib,subprocess,time
r=Path('/tmp/present-xip-on-comparison-20261110');prod=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools');c=Path('/tmp/present-xip-on-clean-source-20261110/components/display/tools/esp_display_present')
shutil.copytree(prod/'esp_display_present',c,symlinks=True,ignore=shutil.ignore_patterns('evidence','build','build-*','.git','__pycache__','managed_components','dist'))
for name in ['esp_lvgl_adapter','esp_lv_present']:(c.parent/name).symlink_to(prod/name,target_is_directory=True)
(r/'deps').symlink_to('/tmp/present-s3-xip-off-all-modes-20261110/deps',target_is_directory=True)
(r/'hardware-policy.json').write_text(json.dumps({'restore_original':False,'reason':'User explicitly requests no original firmware restore'}))
boards={'s3-rgb':'/dev/ttyUSB1','s31-rgb':'/dev/ttyUSB0','p4-dsi':'/dev/ttyACM0'}
for b in boards:
 (r/b).mkdir(exist_ok=True);(r/b/'backup').symlink_to(Path('/tmp/present-xip-matrix-20261110')/b/'backup',target_is_directory=True)
def hashes(root):return {str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(root.rglob('*')) if f.is_file()}
(r/'frozen-source.json').write_text(json.dumps({'present_src':hashes(prod/'esp_display_present/src'),'present_include':hashes(prod/'esp_display_present/include'),'adapter_src':hashes(prod/'esp_lvgl_adapter/src'),'lv_present_src':hashes(prod/'esp_lv_present/src'),'idf_status':subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd='/home/xuxin/esp_work/esp-idf-qspi',text=True)},indent=2)+'\n')
(r/'plan.json').write_text(json.dumps({'boards':boards,'xip':'on','angles':[0],'modes':['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL','TRIPLE_PARTIAL','AUTO'],'cohorts':['clean performance/memory','lightweight address diagnostics'],'expected_supported':66,'expected_skips':6,'expected_scenes':1056,'restore_original':False,'timestamp':time.time(),'clean_source':str(c)},indent=2)+'\n')
print(c)
