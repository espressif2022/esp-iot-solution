"""Normal runner with a recorded per-case planner: do not repeat already collected current cases."""
from pathlib import Path
import sys,importlib.util,json
r=Path(__file__).parent;trace='--driver-trace' in sys.argv
source=r/('diag-source' if trace else 'clean-source')/'components/display/tools/esp_display_present/dev/tools/run.py'
spec=importlib.util.spec_from_file_location('supplement_runner',source);runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
original=runner.cases_for
def planned_cases(board,angles,profiles=None):
 rows=original(board,angles,profiles)
 if not trace:rows=[c for c in rows if not (c['name'] in ('DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','AUTO') and c['rotation'] in (0,90))]
 if board=='s31-rgb' and not trace and (r/'s31-rgb/resume-clean.json').exists():
  seen={tuple(x) for x in json.loads((r/'s31-rgb/resume-clean.json').read_text())['completed']}
  rows=[c for c in rows if (c['name'],c['rotation']) not in seen]
 if board=='p4-dsi':
  ledger=json.loads((r/'p4-dsi/case-ledger.json').read_text()) if (r/'p4-dsi/case-ledger.json').exists() else {'clean':[],'diagnostic':[]}
  seen={(x['mode'],x['rotation']) for x in ledger['diagnostic' if trace else 'clean']}
  rows=[c for c in rows if (c['name'],c['rotation']) not in seen]
  if trace:rows=sorted(rows,key=lambda c:0 if (c['name'],c['rotation'])==('DOUBLE_DIRECT',270) else 1 if (c['name'],c['rotation'])==('DOUBLE_PARTIAL',90) else 2)
 return rows
runner.cases_for=planned_cases
runner.main()
