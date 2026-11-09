import fs from 'node:fs';import assert from 'node:assert/strict';
const root=process.env.PRESENT_ALIGNED_REPORT_ROOT||'/tmp/present-aligned-fill-20261109/preview';const rows=JSON.parse(fs.readFileSync(root+'/comparison-current.json','utf8'));
const address=fs.readFileSync('/tmp/present-aligned-browser.log','utf8').match(/DevTools listening on (ws:\/\/[^\s]+)/)?.[1];assert(address);
const targets=await(await fetch(`http://127.0.0.1:${new URL(address).port}/json/list`)).json(),target=targets.find(x=>x.type==='page');assert(target);
const ws=new WebSocket(target.webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map(),errors=[];
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.method==='Runtime.exceptionThrown')errors.push(m.params.exceptionDetails);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function ev(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw r.exceptionDetails;return r.result.value}
await call('Runtime.enable');await call('Page.enable');await call('Page.navigate',{url:'data:text/html;base64,'+Buffer.from(fs.readFileSync(root+'/comparison.html')).toString('base64')});
for(let i=0;i<60;i++){if(await ev("document.readyState==='complete'&&document.querySelectorAll('section.board').length===6"))break;await new Promise(r=>setTimeout(r,100));}
assert.equal(errors.length,0,JSON.stringify(errors));
const sections=await ev("[...document.querySelectorAll('section.board')].map(s=>({board:s.dataset.board,variant:s.dataset.variant,fps:[...s.querySelectorAll('.fps tbody tr')].map(r=>[...r.children].map(c=>({text:c.textContent,primary:c.firstChild?.textContent,delta:c.querySelector('.delta')?.textContent}))),scenes:[...s.querySelectorAll('.scene-scores tbody tr')].map(r=>[...r.children].map(c=>c.textContent)),capacity:[...s.querySelectorAll('.buffers tbody tr')].map(r=>[...r.children].map(c=>c.textContent)),effective:[...s.querySelectorAll('.effective tbody tr')].map(r=>[...r.children].map(c=>c.textContent)),memory:[...s.querySelectorAll('.memory-value')].map(c=>c.textContent),memoryDeltas:[...s.querySelectorAll('.memory-delta')].map(c=>({bytes:Number(c.dataset.deltaBytes),down:c.classList.contains('down')}))}))");assert.equal(sections.length,6);let fpsCells=0,bufferCells=0,memoryCells=0,deltaCells=0,alignedRows=0;
for(const s of sections){
 const rs=rows.filter(r=>r.board===s.board),modes=[...new Set(rs.map(r=>r.mode))],impls=[s.variant,'adapter'];assert.equal(s.fps.length,modes.length);
 for(let i=0;i<modes.length;i++)for(let a=0;a<4;a++)for(let j=0;j<2;j++){
  const r=rs.find(r=>r.mode===modes[i]&&r.rotation===a*90),impl=impls[j],actual=s.fps[i][1+a*2+j],expected=r[impl+'_lvgl_fps'];
  if(typeof expected==='number')assert.equal(Number(actual.primary),expected);else assert(actual.text.startsWith('—'));
  const matching=r[impl+'_cohort']===r.adapter_cohort,ad=r.adapter_lvgl_fps;
  if(j===0&&typeof expected==='number'&&typeof ad==='number'&&matching){assert(actual.delta);const d=expected-ad;assert(actual.delta.includes(d>0?'+'+d:String(d===0?'0':d)));}else assert.equal(actual.delta,undefined);
  fpsCells++;
 }
 assert.equal(s.capacity.length,modes.length);assert.equal(s.effective.length,rs.length);
 const sceneRows=rs.filter(r=>r[s.variant+'_cohort']==='aligned-v1');assert.equal(s.scenes.length,sceneRows.length);for(let n=0;n<sceneRows.length;n++){const r=sceneRows[n],values=['wallpaper','labels','text'].flatMap(name=>impls.map(impl=>String(r[impl+'_scene_'+name+'_fps']??'—')));assert.deepEqual(s.scenes[n].slice(1),values);}
 const expectedMemory=[],expectedDeltas=[];
 for(let i=0;i<rs.length;i++){
  const r=rs[i];if(r[s.variant+'_cohort']==='aligned-v1')alignedRows++;
  for(let j=0;j<2;j++){
   const impl=impls[j],bytes=r[impl+'_buffer_active_bytes'];
   if(typeof bytes==='number'){assert.equal(s.capacity[modes.indexOf(r.mode)][1+(r.rotation/90)*2+j],`${bytes} · ${r[impl+'_buffer_internal']?'内部RAM':r[impl+'_buffer_psram']?'PSRAM':'其他'}`);assert.equal(s.effective[i][j+1],r[impl+'_effective_fps'].toFixed(2));}
   else assert(s.capacity[modes.indexOf(r.mode)][1+(r.rotation/90)*2+j].startsWith('—'));
   bufferCells++;
  }
  for(const phase of ['before_open','after_open','after_run','after_close'])for(const heap of ['internal','psram'])for(const impl of impls){
   const v=r[`${impl}_${heap}_${phase}_free_bytes`],a=r[`adapter_${heap}_${phase}_free_bytes`];if(typeof v==='number')expectedMemory.push((v/1024).toFixed(2));
   if(impl.startsWith('present')&&typeof v==='number'&&typeof a==='number'&&r[impl+'_cohort']===r.adapter_cohort)expectedDeltas.push({bytes:v-a,down:v-a<0});
  }
 }
 assert.deepEqual(s.memory,expectedMemory);assert.deepEqual(s.memoryDeltas,expectedDeltas);memoryCells+=expectedMemory.length;deltaCells+=expectedDeltas.length;
}
const overview=await ev("[...document.querySelectorAll('#aligned-overview tbody tr')].map(r=>[...r.children].map(c=>c.textContent))");
const overviewExpected=JSON.parse(fs.readFileSync(root+'/analysis-summary.json','utf8'));assert.equal(overview.length,overviewExpected.length);
for(let n=0;n<overview.length;n++){const r=overviewExpected[n],v=overview[n];assert.equal(v.length,4);assert.equal(v[0],r.label);assert.equal(v[1],r.mode_group);assert(v[2].startsWith(r.present_lvgl_score_mean.toFixed(2)));assert(v[2].includes(r.lvgl_delta_percent.toFixed(1)+'%'));assert.equal(v[3],r.adapter_lvgl_score_mean.toFixed(2));}
assert.equal(await ev("document.querySelectorAll('select').length"),0);assert.equal(errors.length,0);
const result={passed:true,sections:sections.length,fpsCells,bufferCells,memoryCells,deltaCells,alignedRows,overviewRows:overview.length,noDropdown:true,runtimeErrors:errors};fs.writeFileSync(root+'/browser-validation.json',JSON.stringify(result,null,2)+'\n');console.log(result);
ws.close();
