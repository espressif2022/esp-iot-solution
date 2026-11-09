import fs from 'node:fs';import assert from 'node:assert/strict';
const root='/home/xuxin/xuxin/esp-dev-kits-gitlab-MR/internal/customer/xiaobai/deliveries/present-s31-dispatch-20261109',R=root+'/evidence/root-cause',data=JSON.parse(fs.readFileSync(R+'/targeted-results.json')),pc=JSON.parse(fs.readFileSync(R+'/pc-results.json')),loops=JSON.parse(fs.readFileSync(R+'/layout-and-pc/loop-layout-proof.json'));
const addr=fs.readFileSync('/tmp/present-aligned-browser.log','utf8').match(/DevTools listening on (ws:\/\/[^\s]+)/)[1],targets=await(await fetch('http://127.0.0.1:'+new URL(addr).port+'/json/list')).json();
const ws=new WebSocket(targets.find(t=>t.type==='page').webSocketDebuggerUrl);await new Promise((ok,no)=>{ws.onopen=ok;ws.onerror=no});let serial=0;const pending=new Map();
ws.onmessage=e=>{const m=JSON.parse(e.data);if(m.id){const p=pending.get(m.id);pending.delete(m.id);m.error?p.reject(m.error):p.resolve(m.result)}};
function call(method,params={}){return new Promise((resolve,reject)=>{const id=++serial;pending.set(id,{resolve,reject});ws.send(JSON.stringify({id,method,params}))})}
async function ev(expression){const r=await call('Runtime.evaluate',{expression,returnByValue:true});assert(!r.exceptionDetails);return r.result.value}
await call('Page.enable');await call('Page.navigate',{url:'data:text/html;base64,'+Buffer.from(fs.readFileSync(root+'/root-cause.html')).toString('base64')});
for(let i=0;i<50;i++){if(await ev("document.querySelectorAll('table').length===5"))break;await new Promise(r=>setTimeout(r,100))}
const tables=await ev("Object.fromEntries([...document.querySelectorAll('table')].map(t=>[t.id,[...t.querySelectorAll('tbody tr')].map(r=>[...r.children].map(c=>c.textContent))]))");
const near=(v,n,t=.000501)=>assert(Math.abs(Number(v)-n)<=t,`${v} != ${n}`);let cells=0;
for(const [id,tags]of Object.entries({plain:['original-before','pad24','original-after'],aligned:['aligned-before','aligned-adapter','aligned-pad24','aligned-after'],font:['font-before','font-adapter','font-pad24','font-after']})){
 assert.equal(tables[id].length,tags.length);
 tags.forEach((tag,i)=>{const x=data.find(x=>x.tag===tag),v=tables[id][i];near(v[1],x.render_per_s);near(v[2],x.instruction_fills_per_render,.50001);near(v[3],x.instruction_stalls_per_render/1e6);near(v[4],x.software_cpu_ms_per_render,.00501);cells+=5})
}
const fns=['lv_font_get_bitmap_fmt_txt','lv_text_get_next_line.part.0','lv_font_get_glyph_dsc_fmt_txt','lv_draw_sw_blend_color_to_rgb565'],tags=['sample-present','sample-adapter','sample-early','sample-pad24'];
fns.forEach((fn,i)=>{assert.equal(tables.hot[i][0],fn);tags.forEach((t,j)=>near(tables.hot[i][j+1],pc.find(x=>x.tag===t).top_functions.find(x=>x.function===fn).ms_per_render,.00501));cells+=5});
loops.forEach((x,i)=>{assert.deepEqual(tables.loop[i],[x.implementation,x.loop_start,String(x.offset_in_cache_line),String(x.cache_lines)]);cells+=4});
assert.equal(await ev("document.querySelectorAll('select').length"),0);const result={passed:true,tables:5,rows:19,cells};fs.writeFileSync(root+'/root-browser-validation.json',JSON.stringify(result,null,2));console.log(result);ws.close();
