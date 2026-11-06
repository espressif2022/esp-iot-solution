from pathlib import Path
import json,csv
b=Path('/tmp/present-refactor-hardware-20261106');rows=[]
for board in ['s3-rgb','p4-dsi']:
 results={v:json.loads((b/v/board/'benchmark/present/results.json').read_text()) for v in ['baseline','current']}
 for v,r in results.items():assert r['complete'] and r['matrix_complete'] and not r['errors'],(board,v)
 lookups={v:{(r['mode'],r['rotation']):r for r in data['rows']} for v,data in results.items()}
 for mode in ['NONE','DOUBLE_DIRECT']:
  for angle in [0,90]:
   a=lookups['baseline'][mode,angle];c=lookups['current'][mode,angle];assert a['status']==c['status']=='PASS'
   rows.append({'board':board,'mode':mode,'rotation':angle,'baseline_lvgl_fps':a['lvgl_fps'],'current_lvgl_fps':c['lvgl_fps'],'delta_fps':c['lvgl_fps']-a['lvgl_fps'],'baseline_cpu':a['cpu'],'current_cpu':c['cpu'],'baseline_render_ms':a['render'],'current_render_ms':c['render'],'baseline_flush_ms':a['flush'],'current_flush_ms':c['flush'],'baseline_transport_fps':a['transport_fps'],'current_transport_fps':c['transport_fps']})
with (b/'comparison.csv').open('w') as f:
 writer=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator="\n");writer.writeheader();writer.writerows(rows)
(b/'comparison.json').write_text(json.dumps(rows,indent=2)+'\n')
lines=['| Board | Mode | Angle | Before FPS | After FPS | Delta |','|---|---|---:|---:|---:|---:|']+[f"| {r['board']} | {r['mode']} | {r['rotation']} | {r['baseline_lvgl_fps']} | {r['current_lvgl_fps']} | {r['delta_fps']:+d} |" for r in rows];(b/'comparison.md').write_text('\n'.join(lines)+'\n');print('\n'.join(lines))
for board in ['s3-rgb','p4-dsi']:
 backup=b/'current'/board/'backup';manifest=json.loads((backup/'manifest.json').read_text());restore=json.loads((backup/'restored.json').read_text());assert restore['verified_full_flash'] and restore['sha256']==manifest['sha256']
print('Both original flash images independently restored and verified')
