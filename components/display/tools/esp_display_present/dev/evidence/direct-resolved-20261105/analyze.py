import json,csv,re
from pathlib import Path
r=Path('/tmp/present-direct-fix-board-20261105');s=json.loads((r/'summary.json').read_text());old=json.loads(Path('/tmp/present-rgb-benchmark-20261105/summary.json').read_text());adapter=json.loads(Path('/tmp/present-rgb-adapter-compare-20261105/summary.json').read_text());rows=[]
for x in s['pass']:
 a=next(z for z in adapter['pass'] if z['mode']==x['mode'] and z['rotation']==x['rotation']);b=next(z for z in old['pass'] if z['mode']==x['mode'] and z['rotation']==x['rotation']);rows.append({'rotation':x['rotation'],'original_adapter_fps':a['lvgl_fps'],'before_fix_fps':b['lvgl_fps'],'after_fix_fps':x['lvgl_fps'],'transport_fps':x['transport_fps'],'render_ms':x['render_ms'],'flush_ms':x['flush_ms']})
if rows:
 with (r/'comparison.csv').open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print(json.dumps(rows,indent=2))
