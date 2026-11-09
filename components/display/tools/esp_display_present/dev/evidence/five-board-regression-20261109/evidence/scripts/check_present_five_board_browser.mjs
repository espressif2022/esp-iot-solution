import fs from 'node:fs';
import assert from 'node:assert/strict';
const root='/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-adapter-five-board-20261109';
const log=fs.readFileSync('/tmp/present-five-board-browser.log','utf8');
const address=log.match(/DevTools listening on (ws:\/\/[^\s]+)/)?.[1];assert(address);
const targets=await(await fetch(`http://127.0.0.1:${new URL(address).port}/json/list`)).json();
const target=targets.find(x=>x.type==='page');assert(target);
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{let id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function evaluate(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value}
await call('Runtime.enable');await call('Page.enable');await call('Page.navigate',{url:`file://${root}/comparison-current.html`});
for(let i=0;i<100;i++){if(await evaluate("document.readyState==='complete' && document.querySelectorAll('.board').length===5"))break;await new Promise(r=>setTimeout(r,100));}
assert.equal(await evaluate('data.length'),120);assert.equal(await evaluate("data.filter(r=>typeof r.present_lvgl_fps==='number').length"),108);assert.equal(await evaluate("data.filter(r=>typeof r.adapter_lvgl_fps==='number').length"),73);
assert.equal(await evaluate("document.querySelectorAll('select').length"),0);assert.equal(await evaluate("data.filter(r=>['s3-rgb','s31-rgb','p4-dsi'].includes(r.board)&&r.mode==='NONE').length"),0);
assert.equal(await evaluate("data.filter(r=>r.board==='s31-qspi'&&!r.mode.includes('TE_')).length"),0);
const boards={};
for(const [board,count] of Object.entries({'s3-rgb':24,'s31-rgb':24,'p4-dsi':24,'s31-qspi':28,'box3-spi':20})){
 const result=await evaluate(`(()=>{let section=document.getElementById('${board}');section.querySelector('details').open=true;return {memoryRows:section.querySelectorAll('.memory tbody tr').length,text:section.innerText}})()`);
 assert.equal(result.memoryRows,count*4);for(const phase of ['打开前剩余','打开后剩余','运行最小剩余','关闭后剩余'])assert(result.text.includes(phase));boards[board]={records:count,memoryRows:result.memoryRows};
}
const memory=await evaluate("(()=>{const r=data.find(r=>r.board==='s3-rgb');return {expected:(r.present_internal_before_open_free_bytes/1024).toFixed(2),actual:document.querySelector('#s3-rgb .memory tbody tr td:nth-child(3)').textContent}})()");assert.equal(memory.actual,memory.expected);assert.equal(errors.length,0);
fs.writeFileSync(root+'/browser-validation.json',JSON.stringify({passed:true,records:120,presentGrades:108,adapterGrades:73,noDropdown:true,rgbDsiNoneExcluded:true,mosaicoTeOnly:true,boards,memory,runtimeErrors:errors},null,2)+'\n');
await call('Browser.close');ws.close();console.log('Browser validation passed');
