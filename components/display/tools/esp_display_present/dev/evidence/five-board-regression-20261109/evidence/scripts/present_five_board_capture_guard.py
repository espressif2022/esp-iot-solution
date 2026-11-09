from pathlib import Path
import json,re,time,os,signal
R=Path('/tmp/present-five-board-20261109');B={b:R for b in ['s3-rgb','p4-dsi','box3-spi']};B.update({'s31-rgb':R/'rgb-retry','s31-qspi':R/'te-retry'});seen={};started=time.monotonic()
while time.monotonic()-started<3600:
 all_done=True
 for b,base in B.items():
  p=base/'stage.json';st=json.loads(p.read_text()).get(b,{}) if p.exists() else {}
  if st.get('stage')!='done':all_done=False
  if st.get('stage')!='capture':continue
  impl=st['implementation'];log=base/b/'benchmark'/impl/'live.log'
  if not log.exists():continue
  text=log.read_text(errors='replace');runs=re.findall(r'RUN mode=([^ ]+).*?rotation=(\d+)',text)
  if not runs:continue
  key=(b,impl);identity=(len(runs),tuple(runs[-1]));old=seen.get(key)
  if not old or old[0]!=identity:seen[key]=(identity,time.monotonic());continue
  if time.monotonic()-old[1]<210 or 'SUITE_PASS ' in text:continue
  for proc in Path('/proc').iterdir():
   if not proc.name.isdecimal():continue
   try:args=(proc/'cmdline').read_bytes().split(b'\0')
   except (OSError,ProcessLookupError):continue
   if b'capture' in args and b.encode() in args and str(base).encode() in args and any(x.endswith(b'run.py') or x.endswith(b'ram_stub_runner.py') for x in args):
    event=dict(board=b,implementation=impl,case=identity,action='SIGINT stalled owned capture; controller will restore',pid=int(proc.name))
    with (R/'capture-guard-events.jsonl').open('a') as f:f.write(json.dumps(event)+'\n')
    os.kill(int(proc.name),signal.SIGINT);print(event,flush=True)
  seen[key]=(identity,time.monotonic())
 if all_done:break
 time.sleep(10)
