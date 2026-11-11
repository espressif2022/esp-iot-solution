from pathlib import Path
import json,re,time
r=Path(__file__).parent
for b in ['p4-dsi','s31-qspi','box3-spi']:
 s=json.loads((r/b/'status.json').read_text());print(b,s['stage'],s.get('variant'),s.get('error',''))
 for p in sorted((r/b/'benchmark').glob('*/live.log')):
  text=p.read_text(errors='replace');runs=re.findall(r'RUN mode=(\w+).*?rotation=(\d+)',text);print(' ',p.parent.name,'PASS',len(re.findall(r'PASS mode=',text,re.M)),'SCENE',len(re.findall(r'SCENE ',text,re.M)),'current',runs[-1] if runs else '-', 'errors',re.findall(r'^.*(?:panic|assert failed|ESP_ERROR_CHECK failed|FAIL ).*$',text,re.M)[-2:])
