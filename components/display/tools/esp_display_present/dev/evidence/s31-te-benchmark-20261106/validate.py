from pathlib import Path
import json
r=Path(__file__).parent;s=json.loads((r/'summary.json').read_text());a=json.loads((r/'results.json').read_text())
checks={'suite_completed':s['complete'],'no_test_errors':not s['errors'],'eight_cases':len(a)==8,'all_combinations':{(x['mode'],x['angle']) for x in a}=={(f'TE_C{c}_T{t}',rot) for c in [1,2] for t in [1,2] for rot in [0,90]},'sixteen_valid_scenes_each':all(x['scenes']==16 for x in a),'dma_count_matches_admission':all(x['admitted']==x['tx_n']==x['frames'] for x in a),'histogram_count_matches':all(sum(x['wait_histogram_counts'])==x['admitted'] for x in a),'minimum_1000_frames_each':all(x['admitted']>=1000 for x in a)}
(r/'validation.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2));assert all(checks.values())
