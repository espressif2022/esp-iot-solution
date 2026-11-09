import fs from 'node:fs';import assert from 'node:assert/strict';
const root=process.env.PRESENT_DISPATCH_REPORT_ROOT||'/tmp/present-s31-dispatch-20261109/preview',data=JSON.parse(fs.readFileSync(root+'/summary.json','utf8'));
const address=fs.readFileSync('/tmp/present-aligned-browser.log','utf8').match(/DevTools listening on (ws:\/\/[^\s]+)/)?.[1];assert(address);
const targets=await(await fetch('http://127.0.0.1:'+new URL(address).port+'/json/list')).json(),target=targets.find(x=>x.type==='page');assert(target);
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function ev(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value}
await call('Runtime.enable');await call('Page.enable');await call('Page.navigate',{url:'data:text/html;base64,'+Buffer.from(fs.readFileSync(root+'/comparison.html')).toString('base64')});
for(let n=0;n<50;n++){if(await ev("document.readyState==='complete'&&document.querySelectorAll('table.cache').length===4"))break;await new Promise(r=>setTimeout(r,100))}
const dom=await ev("[...document.querySelectorAll('table.cache')].map(t=>[...t.querySelectorAll('tbody tr')].map(r=>[...r.children].map(c=>c.textContent)))");assert.equal(dom.length,4);
const names=['Screen sized text','Multiple labels','Multiple arcs','Moving wallpaper'],label={worker:'present worker',inline:'present 调用线程执行',adapter:'adapter'};let cells=0;
const near=(a,b,t=.00501)=>assert(Math.abs(Number(a)-b)<=t,`${a} != ${b}`);
for(let k=0;k<4;k++){
 const rows=data.filter(x=>x.scene===names[k]).sort((a,b)=>['off','on'].indexOf(a.xip)-['off','on'].indexOf(b.xip)||a.rotation-b.rotation||['worker','inline','adapter'].indexOf(a.execution)-['worker','inline','adapter'].indexOf(b.execution));assert.equal(dom[k].length,rows.length);
 for(let n=0;n<rows.length;n++){
  const r=rows[n],v=dom[k][n];assert.equal(v.length,11);assert.equal(v[0],r.xip);assert.equal(v[1],r.rotation+'°');assert.equal(v[2],label[r.execution]);assert.equal(Number(v[3]),r.repeats);
  const nums=v[4].match(/[\d.]+/g).map(Number);near(nums[0],r.render_per_s);near(nums[1],r.render_per_s_min);near(nums[2],r.render_per_s_max);
  near(v[5],r.instruction_accesses_per_render/1e6);near(v[6],r.instruction_fills_per_render,.50001);near(v[7].replace('%',''),r.instruction_fill_access_ratio*100,.000051);near(v[8],r.instruction_stalls_per_render/1e6);near(v[9],r.instruction_stall_fill_ratio);near(v[10],r.instruction_conflicts_per_render);cells+=11;
 }
}
assert.equal(errors.length,0);assert.equal(await ev("document.querySelectorAll('select').length"),0);
const result={passed:true,tables:4,cells,runtimeErrors:errors};fs.writeFileSync(root+'/browser-validation.json',JSON.stringify(result,null,2)+'\n');console.log(result);ws.close();
