from pathlib import Path
import shutil,subprocess,time,os
base=Path('/tmp/present-all-modes-20261107');d=base/'p4-diagnostic';d.mkdir();source=d/'source';shutil.copytree(base/'source',source)
app=source/'components/display/tools/esp_display_present/dev/test_apps/benchmark/main'
f=app/'CMakeLists.txt';s=f.read_text().replace('"${PRESENT_ROOT}/src"','"${PRESENT_ROOT}/src" "${PRESENT_ROOT}/../esp_lv_present/src"');f.write_text(s)
f=app/'bridge_present.c';s=f.read_text().replace('#include "esp_lv_present.h"','#include "esp_lv_present.h"\n#include "present_lvgl.h"\n#include "esp_log.h"\nstatic lv_display_t *diagnostic_display;')
s=s.replace('    return e;\n}\nesp_err_t bench_lock', '    diagnostic_display = *disp;\n    return e;\n}\nesp_err_t bench_lock')
s=s.replace('    esp_err_t e = esp_display_presenter_quiesce(presenter, 10000);','    esp_err_t native = esp_lv_present_native_get_error(lv_display_get_user_data(diagnostic_display));\n    esp_display_presenter_state_t state;\n    esp_err_t query = esp_display_presenter_get_state(presenter, &state);\n    ESP_LOGW("P4_DIAG", "before_close binding=%s state_query=%s state=%d", esp_err_to_name(native), esp_err_to_name(query), (int)state);\n    esp_err_t e = esp_display_presenter_quiesce(presenter, 10000);\n    ESP_LOGW("P4_DIAG", "quiesce=%s", esp_err_to_name(e));')
f.write_text(s)
(d/'deps').symlink_to(base/'deps',target_is_directory=True)
runner=source/'components/display/tools/esp_display_present/dev/tools/run.py'
def cmd(action):
 c=['python3',str(runner),action,'--board','p4-dsi','--implementation','present','--work-root',str(d),'--port','/dev/ttyACM0']
 if action=='build':c+=['--profiles','DOUBLE_DIRECT','--angles','270']
 if action=='capture':c+=['--timeout','300']
 with (d/(action+'.log')).open('w') as f:r=subprocess.run(c,stdout=f,stderr=subprocess.STDOUT)
 print(action,'exit',r.returncode,flush=True)
 if r.returncode:print((d/(action+'.log')).read_text(errors='replace')[-1800:],flush=True)
 return r.returncode
assert cmd('build')==0
while not (base/'p4-dsi/backup/restored.json').exists():time.sleep(1)
(d/'p4-dsi').mkdir(exist_ok=True);(d/'p4-dsi/backup').symlink_to(base/'p4-dsi/backup',target_is_directory=True)
try:
 assert cmd('flash')==0
 result=cmd('capture')
 if not result:cmd('report')
finally:
 assert cmd('restore')==0
print('P4 diagnostic finished, original restored',flush=True)
