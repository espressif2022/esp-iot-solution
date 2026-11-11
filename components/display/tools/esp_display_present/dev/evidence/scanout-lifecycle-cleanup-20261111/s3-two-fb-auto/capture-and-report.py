import sys,json
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,'/tmp/present-lifecycle-cleanup-20261111/two-fb-source/components/display/tools/esp_display_present/dev/tools')
import run
args=SimpleNamespace(board='s3-rgb',suite='benchmark',implementation='present',xip='on',profile=False,driver_trace=None,work_root=Path('/tmp/present-lifecycle-cleanup-20261111/hardware-two-fb'),port='/dev/ttyUSB1',timeout=300,profiles=['AUTO'],angles=[0])
original=run.parse_log
# The stock IDF API logs INVALID_ARG when probing 3 FBs on a 2-FB panel.
# Keep raw serial unchanged; accept only that known count-probe diagnostic.
def parse(text):
 return original('\n'.join(line for line in text.splitlines() if 'invalid frame buffer number' not in line))
run.parse_log=parse
run.capture(args)
root=run.location(args)
text=(root/'live.log').read_text(errors='replace')
expected=[line for line in text.splitlines() if 'invalid frame buffer number' in line]
assert len(expected)==1 and 'rgb_panel' in expected[0],expected
run.report(args)
(root/'probe-observation.json').write_text(json.dumps(dict(actual_driver_fbs=2,requested_mode='AUTO',expected_probe_rejection_lines=expected,raw_log_unchanged=True),indent=2)+'\n')
