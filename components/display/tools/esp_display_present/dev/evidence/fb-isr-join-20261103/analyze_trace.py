from pathlib import Path
from collections import Counter,deque
import json,sys,re,statistics
names={1:'REGISTER',2:'READ_BEGIN',3:'READ_END',4:'SELECT',5:'BOUNCE_EOF_SAMPLED',6:'FRAME_DONE',7:'VSYNC',8:'LINK_SWITCH',9:'RESTART',10:'UNDERRUN',11:'SUBMIT',12:'ARM',13:'CONFIRM',14:'RELEASE',15:'WRITE_BEGIN',16:'WRITE_END',17:'CHAIN',18:'RAW_REQUEST',19:'STOP',20:'INJECT',21:'CALLBACK',22:'APP_BEGIN',23:'APP_COMMIT'}
def parse(path):
 cases=[];current=None
 for raw in path.read_text(errors='replace').splitlines():
  line=raw.strip()
  if line.startswith('BOUNDARY_BEGIN,'):
   _,name,scope,count,dropped=line.split(',');current={'name':name,'scope':scope,'count':int(count),'dropped':int(dropped),'records':[],'ended':False};cases.append(current)
  elif line.startswith('BT,') and current is not None:
   _,seq,t,kind,buf,detail,isr=line.split(',');current['records'].append((int(seq),int(t),int(kind),int(buf,16),int(detail),int(isr)))
  elif line.startswith('BOUNDARY_END,') and current is not None:
   current['ended']=line.split(',')[1]==current['name'];current=None
 return cases

def analyze(c):
 counts=Counter(); selected=None; in_read=set(); last_end={}; problems=[]; uncertain=[]; margins=[]; match=[]; pending=deque(); callback_counts=[]; armed=False; callbacks=0; last_time=-1
 for index,(seq,t,k,b,d,isr) in enumerate(c['records']):
  counts[names.get(k,str(k))]+=1
  if seq!=index or t<last_time: uncertain.append(f'non-contiguous sequence or timestamp at {seq}')
  last_time=t
  if k==2: in_read.add(b)
  elif k==3:
   in_read.discard(b);last_end[b]=t
  elif k==4:
   selected=b
   if pending:
    found=next((i for i,p in enumerate(pending) if p[0]==b),None)
    if found is not None:
     if found: uncertain.append(f'{found} earlier submission(s) unmatched at sequence {seq}')
     while found>0: pending.popleft();found-=1
     target,cookie,start=pending.popleft();match.append(t-start)
  elif k==11: pending.append((b,d,t))
  elif k==12: armed=True;callbacks=0
  elif k==21 and armed and d: callbacks+=1
  elif k==13:
   if armed: callback_counts.append(callbacks);armed=False
  elif k==14:
   if b==selected: problems.append(f'release selected FB {b:#x} seq={seq}')
   if b in in_read: problems.append(f'release while read active {b:#x} seq={seq}')
   if b in last_end: margins.append(t-last_end[b])
   else: uncertain.append(f'no preceding read end for release {b:#x} seq={seq}')
  elif k==15:
   if b==selected: problems.append(f'write permission granted on selected FB {b:#x} seq={seq}')
   if b in in_read: problems.append(f'write permission granted while read active {b:#x} seq={seq}')
  elif k==9:
   uncertain.append(f'driver restart seq={seq} reason={d:#x}')
   in_read.discard(b)
  elif k==10: problems.append(f'driver underrun seq={seq}')
 if not c['ended'] or len(c['records'])!=c['count'] or c['dropped']:
  uncertain.append('capture incomplete or dropped')
 if pending: uncertain.append(f'{len(pending)} submissions have no matched subsequent selection')
 if not counts['STOP']: uncertain.append('missing successful stop')
 if counts['WRITE_BEGIN']!=counts['WRITE_END']: uncertain.append('unfinished write interval')
 if counts['APP_BEGIN']!=counts['APP_COMMIT'] or counts['RELEASE']!=counts['APP_COMMIT']: uncertain.append('incomplete application or release count')
 def span(v):
  return {'min':min(v),'mean':round(statistics.mean(v),1),'max':max(v),'n':len(v)} if v else None
 return {'name':c['name'],'scope':c['scope'],'capture_count':len(c['records']),'reported_count':c['count'],'dropped':c['dropped'],'counts':dict(counts),'submit_to_first_matching_select_us':span(match),'last_read_end_to_release_us':span(margins),'callbacks_until_confirm':span(callback_counts),'problems':problems,'uncertainty':uncertain}
if __name__=='__main__':
 files=[Path(x) for x in sys.argv[1:]]
 results={p.name:[analyze(c) for c in parse(p)] for p in files}
 print(json.dumps(results,ensure_ascii=False,indent=2))
