// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Requires a local Chromium instance with --remote-debugging-port. No hardware access.
import fs from 'node:fs';
import assert from 'node:assert/strict';
const file=process.argv[2];
assert(file, 'Usage: node check_comparison.mjs <html> [CDP port=9333] [output directory=/tmp]');
const output=process.argv[4]||'/tmp';
const targets=await(await fetch(`http://127.0.0.1:${process.argv[3]||9333}/json/list`)).json();
const ws=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);
await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j});
let seq=0;const waiting=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params);if(m.id){const p=waiting.get(m.id);waiting.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
const call=(method,params={})=>new Promise((resolve,reject)=>{const id=++seq;waiting.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))});
async function ev(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value;}
await call('Runtime.enable');await call('Page.enable');
await call('Emulation.setDeviceMetricsOverride',{width:1440,height:1050,deviceScaleFactor:1,mobile:false});
await call('Page.navigate',{url:'data:text/html;base64,'+Buffer.from(fs.readFileSync(file)).toString('base64')});
for(let i=0;i<50;i++){if(await ev("document.readyState==='complete'&&document.querySelectorAll('.board').length===3"))break;await new Promise(r=>setTimeout(r,100));}
assert.deepEqual(errors,[]);
const D=await ev('D');assert.equal(D.lifecycle.count,24);
const modes=D.modes,angles=D.angles;
let values=0,origins=0,selectors=0;
for(const board of D.boards){
 const id=board.id;
 await ev(`document.getElementById('tab-${id}').click()`);
 assert.deepEqual(await ev("Array.from(document.querySelectorAll('.board')).filter(b=>!b.hidden).map(b=>b.id)"),[id]);
 const fresh=board.cohorts.clean.present.filter(r=>r.origin==='新测 11-11');assert.equal(fresh.length,8);origins+=fresh.length;
 assert((await ev(`document.querySelector('#${id} .accounting').textContent`)).includes('本轮未重采 ELF'));
 assert((await ev(`document.querySelector('#${id} .diagnostics-panel > summary').textContent`)).includes('历史 11-10'));
 for(const m of modes){
  await ev(`(()=>{for(const cls of ['memory-mode','scene-mode']){const s=document.querySelector('#${id} .'+cls);s.value='${m}';s.dispatchEvent(new Event('change'));}})()`);selectors+=2;
  for(const metric of ['free','used']){
   await ev(`(()=>{const s=document.querySelector('#${id} .memory-metric');s.value='${metric}';s.dispatchEvent(new Event('change'));})()`);
   const tables=await ev(`Array.from(document.querySelectorAll('#${id} .memory')).map(t=>({heap:t.dataset.heap,rows:Array.from(t.querySelectorAll('tbody tr')).map(tr=>({phase:tr.dataset.phase,cells:Array.from(tr.querySelectorAll('td[data-impl]')).map(c=>({value:c.querySelector('.value')?.textContent,delta:c.querySelector('.delta')?.className,text:c.textContent}))}))}))`);
   for(const table of tables)for(const tr of table.rows)for(let ai=0;ai<4;ai++)for(let ii=0;ii<2;ii++){
    const impl=['present','adapter'][ii],r=board.cohorts.clean[impl].find(r=>r.mode===m&&r.rotation===angles[ai]),cell=tr.cells[ai*2+ii];
    if(r.status==='SKIP'){assert.equal(cell.text,'−');continue;}
    const key=table.heap+(tr.phase==='after_run'?'_min_free':'_free');
    const get=r=>(metric==='free'?r.memory[tr.phase][key]:r.memory[tr.phase][table.heap+'_total']-r.memory[tr.phase][key])/1024;
    assert(Math.abs(Number(cell.value)-get(r))<=0.0051);assert(cell.text.includes(r.origin));values++;
    const other=board.cohorts.clean.adapter.find(r=>r.mode===m&&r.rotation===angles[ai]);
    if(ii===0&&other.status==='PASS'){const diff=get(r)-get(other);assert(cell.delta.includes(diff===0?'same':((diff>0)===(metric==='free')?'up':'down')));}
   }
  }
  const scenes=await ev(`Array.from(document.querySelectorAll('#${id} .scenes tbody tr')).map(tr=>({name:tr.cells[0].textContent,cells:Array.from(tr.querySelectorAll('td[data-impl]')).map(c=>c.querySelector('.value')?.textContent)}))`);
  assert.equal(scenes.length,16);
  for(const tr of scenes)for(let ai=0;ai<4;ai++)for(let ii=0;ii<2;ii++){
   const r=board.cohorts.clean[['present','adapter'][ii]].find(r=>r.mode===m&&r.rotation===angles[ai]);
   if(r.status==='SKIP')continue;assert.equal(Number(tr.cells[ai*2+ii]),r.scene_scores.find(s=>s.name===tr.name).fps);values++;
  }
 }
 const fps=await ev(`Array.from(document.querySelectorAll('#${id} .fps tbody tr')).map(tr=>Array.from(tr.querySelectorAll('td[data-impl]')).map(c=>c.querySelector('.value')?.textContent))`);
 for(let mi=0;mi<modes.length;mi++)for(let ai=0;ai<4;ai++)for(let ii=0;ii<2;ii++){
  const r=board.cohorts.clean[['present','adapter'][ii]].find(r=>r.mode===modes[mi]&&r.rotation===angles[ai]);
  if(r.status==='SKIP')continue;assert.equal(Number(fps[mi][ai*2+ii]),r.lvgl_fps);values++;
 }
 assert(await ev(`Array.from(document.querySelectorAll('#${id} .transport td.a')).every(c=>c.textContent==='−')`));
 await ev("document.getElementById('collapse-all').click()");assert(await ev(`Array.from(document.querySelectorAll('#${id} details.panel')).every(p=>!p.open)`));
 await ev("document.getElementById('expand-all').click()");assert(await ev(`Array.from(document.querySelectorAll('#${id} details.panel')).every(p=>p.open)`));
}
assert.equal(origins,24);assert.equal(values,4356);
await ev("document.getElementById('tab-s3-rgb').click();document.querySelectorAll('#s3-rgb details.panel').forEach(p=>p.open=p.classList.contains('fps-panel')||p.classList.contains('memory-panel'));document.querySelector('#s3-rgb .memory-mode').value='DOUBLE_FULL';document.querySelector('#s3-rgb .memory-mode').dispatchEvent(new Event('change'));scrollTo(0,0)");
const shot=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(output+'/desktop.png',Buffer.from(shot.data,'base64'));
await call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
assert(await ev('document.documentElement.scrollWidth<=innerWidth'));
const mobile=await call('Page.captureScreenshot',{format:'png'});fs.writeFileSync(output+'/mobile.png',Buffer.from(mobile.data,'base64'));
assert.deepEqual(errors,[]);
console.log(JSON.stringify({passed:true,values,origins,selectors,errors:errors.length,desktop:1440,mobile:390}));
ws.close();
