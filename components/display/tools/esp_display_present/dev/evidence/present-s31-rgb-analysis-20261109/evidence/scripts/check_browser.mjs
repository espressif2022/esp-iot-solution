import fs from 'node:fs';import assert from 'node:assert/strict';
const root='/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-rgb-analysis-20261109';
const rows=JSON.parse(fs.readFileSync(root+'/results.json','utf8')),keys=[['DOUBLE_PARTIAL',0],['DOUBLE_PARTIAL',90],['TRIPLE_PARTIAL',0],['TRIPLE_PARTIAL',90]];
assert.equal(rows.length,40);assert(!rows.some(r=>r.variant==='tile2'));
const address=fs.readFileSync('/tmp/present-s31-analysis-browser.log','utf8').match(/DevTools listening on (ws:\/\/[^\s]+)/)?.[1];assert(address);
const targets=await(await fetch(`http://127.0.0.1:${new URL(address).port}/json/list`)).json(),target=targets.find(x=>x.type==='page');assert(target);
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function evaluate(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value}
await call('Runtime.enable');await call('Page.enable');await call('Page.navigate',{url:`file://${root}/comparison.html`});
for(let i=0;i<100;i++){if(await evaluate("document.readyState==='complete'&&document.querySelectorAll('#matched tbody tr').length===4"))break;await new Promise(r=>setTimeout(r,100));}
let checked=0;
for(const [id,variants,profile] of [['profile',[['baseline','present'],['burst128','present'],['cache_contract','present'],['burst_cache','present']],true],['control',[['baseline','present'],['burst_cache','present'],['baseline','adapter']],false],['xip',[['baseline','present'],['baseline','adapter'],['xip','present'],['xip','adapter']],false],['matched',[['xip','present'],['xip','adapter'],['adapter_matched','adapter']],false]]){
 const actual=await evaluate(`[...document.querySelectorAll('#${id} tbody tr')].map(tr=>[...tr.querySelectorAll('.grade')].map(td=>Number(td.textContent)))`);assert.equal(actual.length,4);
 for(let n=0;n<4;n++)for(let j=0;j<variants.length;j++){const [v,i]=variants[j],r=rows.find(r=>r.variant===v&&r.implementation===i&&r.profile===profile&&r.mode===keys[n][0]&&r.rotation===keys[n][1]);assert(r);assert.equal(actual[n][j],r.lvgl_fps);checked++;}
}
const heads=await evaluate("[...document.querySelectorAll('#profile th')].map(x=>x.textContent)");assert(!heads.some(x=>x.includes('tile')));assert.equal(errors.length,0);
const metricRows=await evaluate("document.querySelectorAll('#metrics tbody tr').length");assert.equal(metricRows,160);
fs.writeFileSync(root+'/browser-validation.json',JSON.stringify({passed:true,comparisonCases:40,fpsCellsChecked:checked,metricRows,singleTile:true,runtimeErrors:errors},null,2)+'\n');
await call('Browser.close');ws.close();console.log({checked,metricRows});
