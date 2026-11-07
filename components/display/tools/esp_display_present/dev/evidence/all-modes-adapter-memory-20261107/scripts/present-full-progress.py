from pathlib import Path
import re,json,datetime,sys
b=Path('/tmp/present-all-modes-20261107')
print(datetime.datetime.now(datetime.timezone.utc).isoformat())
for board in ['s3-rgb','p4-dsi','s31-qspi','box3-spi']:
 data={'board':board,'backup':(b/board/'backup/manifest.json').exists(),'restored':(b/board/'backup/restored.json').exists()}
 for impl in ['present','adapter']:
  p=b/board/'benchmark'/impl
  info={'build':(p/'manifest.json').exists()}
  log=p/'live.log'
  if log.exists():
   text=log.read_text(errors='replace')
   info['passed']=len(re.findall(r'\bPASS mode=',text));info['skipped']=len(re.findall(r'\bSKIP mode=',text));info['complete']='SUITE_PASS ' in text
   run=re.findall(r'RUN mode=(\S+).*rotation=(\d+)',text);info['active']=run[-1] if run else None
   errors=[l for l in text.splitlines() if any(x in l for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed','Stack canary'])];info['errors']=errors[-2:]
   matches=re.findall(r'PASS mode=([^\n]+)',text);info['latest_result']=matches[-1] if matches else None
  data[impl]=info
 event=b/(board+'-events.jsonl')
 if event.exists():data['last_event']=json.loads(event.read_text().splitlines()[-1])
 if board=='p4-dsi':
  follow=b/'p4-followup/p4-dsi/benchmark/present/live.log'
  if follow.exists():
   tx=follow.read_text(errors='replace');runs=re.findall(r'RUN mode=(\S+).*rotation=(\d+)',tx)
   data['followup']={'passed':len(re.findall(r'\bPASS mode=',tx)),'active':runs[-1] if runs else None,'errors':[l for l in tx.splitlines() if any(x in l for x in ['assert failed','Guru Meditation','ESP_ERROR_CHECK failed'])][-2:]}
 if board=='p4-dsi' and (b/'p4-isolated-cases').exists():
  zs=[json.loads(f.read_text()) for f in (b/'p4-isolated-cases').glob('*/results.json')]
  data['isolated']={'completed':len(zs),'passed':sum(z['complete'] and z['matrix_complete'] and z.get('memory_complete',False) and not z['errors'] for z in zs)}
 if board=='p4-dsi' and (b/'p4-adapter-isolated-cases').exists():
  zs=[json.loads(f.read_text()) for f in (b/'p4-adapter-isolated-cases').glob('*/results.json')]
  data['adapter_isolated']={'completed':len(zs),'passed':sum(z['complete'] and z['matrix_complete'] and z.get('memory_complete',False) and not z['errors'] for z in zs)}
 data['restored'] = data['restored'] and data.get('last_event',{}).get('stage')=='RESTORE_VERIFIED'
 if '--short' in sys.argv:
  print(board, ' / '.join(f"{impl}: {data[impl].get('passed',0)} PASS, {data[impl].get('skipped',0)} SKIP, active={data[impl].get('active')}, errors={data[impl].get('errors',[])}" for impl in ['present','adapter']), 'event='+str(data.get('last_event',{}).get('stage')), 'restored='+str(data['restored']), 'followup='+str(data.get('followup',{})), 'isolated='+str(data.get('isolated',{})), 'adapter_isolated='+str(data.get('adapter_isolated',{})))
 else:
  print(json.dumps(data,ensure_ascii=False))
