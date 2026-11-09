import fs from 'node:fs';import assert from 'node:assert/strict';
const root='/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-deep-analysis-20261109';
const data=JSON.parse(fs.readFileSync(root+'/results.json','utf8')),summaries=JSON.parse(fs.readFileSync(root+'/summaries.json','utf8'));
const address=fs.readFileSync('/tmp/present-s31-deep-browser.log','utf8').match(/DevTools listening on (ws:\/\/[^\s]+)/)?.[1];assert(address);
const targets=await(await fetch(`http://127.0.0.1:${new URL(address).port}/json/list`)).json(),target=targets.find(x=>x.type==='page');assert(target);
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function ev(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value}
await call('Runtime.enable');await call('Page.enable');await call('Page.navigate',{url:`file://${root}/comparison.html`});
for(let i=0;i<50;i++){if(await ev("document.readyState==='complete' && document.querySelectorAll('table').length===6"))break;await new Promise(r=>setTimeout(r,100));}
function compareRow(actual,expected){assert.equal(actual.length,expected.length);for(let i=0;i<expected.length;i++){if(typeof expected[i]==='number'){assert.match(actual[i],/^-?\d+\.\d{2}$/);assert(Math.abs(Number(actual[i])-expected[i])<=0.005000001,`${actual[i]} != ${expected[i]}`);}else assert.equal(actual[i],expected[i]);}}
const tables=await ev("[...document.querySelectorAll('table')].map(t=>({headers:[...t.querySelectorAll('th')].map(x=>x.textContent),rows:[...t.querySelectorAll('tbody tr')].map(r=>[...r.children].map(x=>x.textContent))}))");assert.equal(tables.length,6);assert.equal(tables[0].rows.length,4);let cells=0;
for(let i=0;i<4;i++){
 const v=i<2?'off':'on',a=i%2*90,p=summaries.find(c=>c.variant===v&&c.implementation==='present'&&c.rotation===a),q=summaries.find(c=>c.variant===v&&c.implementation==='adapter'&&c.rotation===a);
 const expected=[v,String(a),...['grade','render_fps','panel_fps','empty_pct','stall_m_frame','sw_ms_frame'].flatMap(k=>[p,q].map(s=>s[k]))];compareRow(tables[0].rows[i],expected);cells+=expected.length;
 assert.equal(tables[i+1].rows.length,16);
 for(let j=0;j<16;j++){
  const p=data.find(c=>c.variant===v&&c.implementation==='present'&&c.rotation===a).scenes[j],q=data.find(c=>c.variant===v&&c.implementation==='adapter'&&c.rotation===a).scenes[j];
  const expected=[p.name,...['render_per_s','empty_pct','i_stall_m_per_render','software_draw_ms_per_render','wait_ms_per_render','cpu_busy_pct'].flatMap(k=>[p,q].map(s=>s[k])),p.present_workers_ms_per_render];compareRow(tables[i+1].rows[j],expected);cells+=expected.length;
 }
}
assert.equal(tables[5].rows.length,10);for(const t of tables)for(const row of t.rows)assert.equal(row.length,t.headers.length);
assert.equal(await ev("document.querySelectorAll('select').length"),0);assert.equal(errors.length,0);
fs.writeFileSync(root+'/browser-validation.json',JSON.stringify({passed:true,cases:8,scenes:128,comparedCells:cells,tableRows:tables.map(t=>t.rows.length),noDropdown:true,runtimeErrors:errors},null,2)+'\n');
await call('Browser.close');ws.close();console.log({cells,tables:tables.length});
