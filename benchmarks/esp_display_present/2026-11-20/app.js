const $=s=>document.querySelector(s),angles=[0,90,180,270],esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));let board='s3-rgb';
function table(h,rows){return '<div class="scroll"><table><thead><tr>'+h.map(x=>'<th>'+x+'</th>').join('')+'</tr></thead><tbody>'+rows.map(row=>'<tr>'+row.map((v,i)=>'<td class="'+(i%2?'p':'a')+'">'+v+'</td>').join('')+'</tr>').join('')+'</tbody></table></div>'}
function diff(p,a){if(p==null||a==null||!a||p===a)return '';let d=(p/a-1)*100;if(!Number(d.toFixed(1)))return '';return '<span class="delta '+(d>0?'good':'bad')+'">'+(d>0?'↑ +':'↓ ')+d.toFixed(1)+'%</span>'}
function key(r){return r.mode+(r.mode.includes('PARTIAL')?' · D'+r.tiles:'')}
// Current policy has D1 sync and D2 async only. Retired combinations remain in exports.
function visibleMode(r){return key(r)}
function row(impl,mode,a){return DATA.boards[board].rows.find(r=>r.implementation===impl&&key(r)===mode&&r.rotation===a)}
function epochNote(r){return ''}
// Legacy adapter NONE_BACKGROUND_* IDs describe direct LVGL-task submission.
function reference(mode,a){if(mode.startsWith('TE_C'))return row('adapter',teReferenceMode(mode,a),a);return row('adapter',mode,a)}
function value(r){return r?.lvgl_fps??'—'}
function inputInfo(mode,implementation='present'){
 const rows=DATA.boards[board].rows.filter(r=>r.implementation===implementation&&visibleMode(r)===mode);
 const audits=rows.map(r=>r.buffer_audit).filter(Boolean);
 const domains=[...new Set(audits.map(a=>a.internal&&!a.psram?'SRAM':a.psram&&!a.internal?'PSRAM':'Mixed'))];
 const captions=[...new Set(audits.map(a=>a.active_bytes===a.logical_w*a.logical_h*2?'Full screen':a.active_bytes&&a.logical_w?(a.active_bytes/(a.logical_w*2))+' lines':'').filter(Boolean))];
 return {domain:domains.length===1?domains[0]:domains.length?'Mixed':'—',capacity:captions.join(' / ')};
}
function inputDomain(mode){return inputInfo(mode).domain;}
function modeFormat(mode,implementation='present'){
 const none=mode.match(/^NONE_(CALLER|BACKGROUND|DEFAULT)_D([12])_(INTERNAL|PSRAM)$/),partial=mode.match(/^(DOUBLE|TRIPLE)_PARTIAL · D([12])$/),te=mode.match(/^TE_C([12])_D([12])$/);
 const fb=mode.match(/^(DOUBLE|TRIPLE)_(FULL|DIRECT)$/),info=inputInfo(mode,implementation);
 const domain=info.domain==='—'&&none?(none[3]==='INTERNAL'?'SRAM':'PSRAM'):info.domain;
 const suffix=' · '+domain+(info.capacity?' · '+info.capacity:'');
 if(fb){const n=fb[1]==='DOUBLE'?'2':'3';return {id:fb[2],name:fb[2],frameBuffers:n,domain,child:n+'FB'+suffix}}
 if(none&&none[1]==='DEFAULT')return {id:'NONE_DEFAULT',name:'NONE · 自动提交策略',tile:none[2],domain,child:'D'+none[2]+suffix+' · '+(none[2]==='1'?'Sync Submit':'Async Submit')};
 if(none){const policy=implementation==='adapter'||none[1]==='CALLER'?'Sync Submit':'Async Submit';return {id:implementation==='adapter'?'NONE_CALLER':'NONE_'+none[1],name:'NONE · '+policy,tile:none[2],domain,child:'D'+none[2]+suffix}}
 if(partial){const n=partial[1]==='DOUBLE'?'2':'3';return {id:'PARTIAL',name:'PARTIAL',frameBuffers:n,tile:partial[2],domain,child:n+'FB · D'+partial[2]+suffix}}
 if(te)return {id:'TE_C'+te[1],name:'TE · '+te[1]+'FB',tile:te[2],domain,child:'D'+te[2]+suffix};
 return {id:mode,name:mode.replace(/^ADAPTER_TE_/,'TE · ')+(info.capacity?' · '+domain+' · '+info.capacity:''),child:''};
}
function label(mode,implementation='present'){
 const f=modeFormat(mode,implementation);
 return f.name+(f.child?' · '+f.child:'');
}
function teReferenceMode(mode,a){return row('present',mode,a)?.te_layout?.closest_adapter_mode;}
function teDescription(mode,a){
 const layout=row('present',mode,a)?.te_layout;
 if(!layout)return '';
 return '近似流水线对照：'+layout.closest_adapter_mode.replace('ADAPTER_TE_','')+'；Present '+(layout.staging_buffer?'有 staging':'PSRAM 直发、无 staging')+'。输入 tile 数、LVGL 渲染模式及搬运粒度不同，升降仅表示当前配置的实测差异。';
}
function pathNote(r){
 if(!r)return '';
 const observed=r.io_path;let caption='',detail='';
 if(observed?.kind==='direct_configured'||observed?.kind==='staging_configured'){const stage=observed.kind==='staging_configured';return '<span class="metric-note transfer-path" title="按本轮旋转、输入内存域、紧密 stride 与 DMA 配置推导，未采每笔命中次数；驱动仍逐事务检查对齐及中转条件。">'+(stage?'Staging':'Direct')+'</span>';}
 if(observed&&Number.isFinite(observed.direct)&&Number.isFinite(observed.staging)){
  caption=observed.direct&&observed.staging?'Direct + Staging':observed.staging?'Staging':observed.direct?'Direct':'';
  detail='整组 benchmark 实测：Direct '+observed.direct+' 次，Staging '+observed.staging+' 次；统计成功接受的逻辑发送，不是 DMA 分块数。';
 }else if(observed?.kind==='driver_staging'||(r.implementation==='adapter'&&r.buffer_audit?.psram&&board!=='s31-qspi'&&!r.mode.endsWith('BOUNCE'))){caption='Staging · driver';detail='该组PSRAM直接DMA关闭，adapter将PSRAM像素交给SPI驱动，驱动分块中转到内部DMA内存；由配置和驱动实现确认，未注入内部事务计数。';}
 else if(observed?.kind==='direct'||(r.implementation==='adapter'&&r.mode.startsWith('NONE_'))){caption='Direct';detail='adapter NONE 由 LVGL 任务直接调用驱动；PSRAM DMA 允许直发仍须满足每个驱动事务的地址/长度对齐，否则驱动可分配临时缓冲。未注入驱动内部路径计数。';}
 else if(r.io_staging){caption=r.io_staging.transfer_policy==='required'?'Staging':'Direct / Staging';detail='本轮配置推导，未采发送路径命中次数；Direct / Staging 表示按 DMA、对齐与 stride 条件选择。';}
 else if(r.te_layout){caption=r.te_layout.staging_buffer?'Staging':'Direct';detail='根据该 TE 配置的输出 staging 选择标注，本轮未采发送路径命中次数。';}
 else if(r.mode.startsWith('ADAPTER_TE_')){caption=r.mode.endsWith('BOUNCE')?'Staging':'Direct';detail='adapter 原生 TE 的显式 bounce 配置；本轮未采发送路径命中次数。';}
 if(!caption)return '';
 return '<span class="metric-note transfer-path" title="'+esc(detail+' Direct 指框架输出直接交驱动，不表示 LVGL DIRECT 渲染，也不排除驱动内部 bounce。')+'">'+caption+'</span>';
}
function overviewGroups(modes,te){
 const groups=[];
 for(const m of [...modes,...te.filter(m=>m.startsWith('TE_C'))]){
  const f=modeFormat(m);let g=groups.find(x=>x.id===f.id);
  if(!g){g={id:f.id,name:f.name,split:!!f.child,entries:[]};groups.push(g)}
  g.entries.push({mode:m,...f});
 }
 const order=['FULL','DIRECT','PARTIAL','NONE_DEFAULT','NONE_CALLER','NONE_BACKGROUND','TE_C1','TE_C2'];
 groups.sort((a,b)=>(order.indexOf(a.id)<0?99:order.indexOf(a.id))-(order.indexOf(b.id)<0?99:order.indexOf(b.id)));
 for(const g of groups)if(g.split)g.entries.sort((a,b)=>Number(a.frameBuffers??0)-Number(b.frameBuffers??0)||['SRAM','PSRAM','Mixed','—'].indexOf(a.domain)-['SRAM','PSRAM','Mixed','—'].indexOf(b.domain)||Number(a.tile??0)-Number(b.tile??0));
 return groups;
}
function overviewTable(modes,te,metric){
 const groups=overviewGroups(modes,te);
 const rate=r=>r?.[metric]==null?'—':metric==='lvgl_fps'?String(r[metric]):r[metric].toFixed(2);
 const head='<tr><th rowspan="2" scope="col">模式</th>'+angles.map(a=>'<th colspan="2" scope="colgroup">'+a+'°</th>').join('')+'</tr><tr>'+angles.map(()=>'<th class="p" scope="col">Present</th><th class="a" scope="col">adapter</th>').join('')+'</tr>';
 const body=groups.map(g=>{
  let note='';
  if(g.id.startsWith('TE_C')){const layout=row('present',g.entries[0].mode,0)?.te_layout;if(layout)note='<span class="metric-note">FB: '+esc(layout.image_memory)+' · Full screen · ≈ '+esc(layout.closest_adapter_mode.replace('ADAPTER_TE_',''))+'</span>'}
  const makeRow=e=>{
   const m=e.mode;
   return '<tr'+(g.split?' class="mode-child"':'')+' data-mode="'+esc(m)+'"><th scope="row">'+esc(g.split?e.child:g.name)+'</th>'+angles.map(a=>{
    const p=row('present',m,a),ad=reference(m,a);
    return ['present','adapter'].map(impl=>{
     const sample=impl==='present'?p:ad;
     return '<td class="'+(impl==='present'?'p':'a')+'" data-implementation="'+impl+'" data-mode="'+esc(m)+'" data-angle="'+a+'"'+(e.tile?' data-tile="'+e.tile+'"':'')+(e.frameBuffers?' data-frame-buffers="'+e.frameBuffers+'"':'')+'>'+(impl==='present'?'<span class="score"><span class="score-value">'+rate(p)+'</span><span class="score-change">'+diff(p?.[metric],ad?.[metric])+'</span></span>':rate(ad))+pathNote(sample)+epochNote(sample)+'</td>';
    }).join('');
   }).join('')+'</tr>';
  };
  return '<tbody data-group="'+esc(g.id)+'">'+(g.split?'<tr class="mode-group"><th colspan="9" scope="rowgroup">'+esc(g.name)+note+'</th></tr>':'')+g.entries.map(makeRow).join('')+'</tbody>';
 }).join('');
 return '<div class="scroll"><table class="overview-table" data-metric="'+metric+'"><thead>'+head+'</thead>'+body+'</table></div><p class="small">FULL / DIRECT / PARTIAL 为并列渲染模式；2FB / 3FB 是模式帧缓冲数，不含额外旋转画布。D1 / D2 是输入缓冲数；容量按实测缓冲审计标注行数或 Full screen。Full screen 仅表示每块可容纳整屏，NONE 仍为 PARTIAL 渲染，不自动变成 DIRECT 或强制全屏重绘。Sync / Async Submit 指 LVGL / 后台任务推进，DMA 完成都可异步。FPS 下灰字是该角度的框架输出路径；Direct + Staging 为实测混用，Direct / Staging 根据配置推导、未采命中次数；Staging · driver 单列已确认的驱动内部中转；Direct 不保证驱动内部零拷贝。adapter NONE 对应 Sync Submit，原生不支持项为 —。</p>';
}
function selectedPathSummary(p,ad,angle){
 const parts=[['Present',p],['adapter',ad]].map(([name,r])=>{
  const note=pathNote(r);if(!note)return '';
  const q=r.io_path,measured=q&&Number.isFinite(q.direct)&&Number.isFinite(q.staging);
  const counts=measured?' · Direct '+q.direct.toLocaleString()+' / Staging '+q.staging.toLocaleString():'';
  return '<div data-path-implementation="'+(name==='Present'?'present':'adapter')+'"><strong>'+name+'</strong>'+note+(counts?'<span class="metric-note path-counts">'+counts+'</span>':'')+'</div>';
 }).filter(Boolean);
 return parts.length?'<div class="path-summary"><span class="metric-note">Output path · '+angle+'° · 整组测试统计（含预热），非单场景计数</span><div class="path-pair">'+parts.join('')+'</div></div>':'';
}
function memorySample(impl,mode,angle,original){
 const sample=DATA.boards[board].memory_samples?.find(r=>r.implementation===impl&&key(r)===mode&&r.rotation===angle);
 return sample?{...original,memory_aligned:sample}:original;
}
function memoryValue(p,a,higher,unit,compare){
 if(p==null)return '—';
 let text=(unit==='B'?p.toFixed(0):(p/1024).toFixed(1))+' '+unit;
 if(!compare||a==null)return text;
 const d=p-a,display=unit==='B'?d:Math.round(d/1024*10)/10;
 if(!display)return text;
 return text+'<span class="delta '+((higher?d>0:d<0)?'good':'bad')+'">'+(d>0?'+':'−')+Math.abs(display).toFixed(unit==='B'?0:1)+' '+unit+'</span>';
}
function scene(){
 if($('#mode').value.startsWith('TE_C')&&!$('#teReference').dataset.manual){const ref=teReferenceMode($('#mode').value,Number($('#angle').value));if(ref)$('#teReference').value=ref;}
 const mode=$('#mode').value,a=Number($('#angle').value),p=row('present',mode,a);
 let ad=reference(mode,a);
 const isTE=mode.startsWith('TE_C');
 if(isTE)ad=row('adapter',$('#teReference').value,a);
 $('#teReference').hidden=!isTE;
 $('#selected-path').innerHTML=selectedPathSummary(p,ad,a);
 const compare=!!p&&!!ad,rs=[];
 for(let i=0;i<16;i++){
  const x=p?.raw_scenes[i],y=ad?.raw_scenes[i],u=p?.unified_scenes[i],v=ad?.unified_scenes[i];
  if(!x&&!y)continue;
  const pu=u?u.rendered*1e6/u.elapsed_us:null,au=v?v.rendered*1e6/v.elapsed_us:null;
  rs.push([esc(x?.name??y.name),String(x?.fps??'—')+(compare?diff(x?.fps,y?.fps):''),y?.fps??'—',
   x?x.render+' / '+x.flush:'—',y?y.render+' / '+y.flush:'—',
   pu==null?'—':pu.toFixed(2)+(compare?diff(pu,au):''),au==null?'—':au.toFixed(2),
   u?Math.round(u.pixels/u.rendered).toLocaleString():'—',v?Math.round(v.pixels/v.rendered).toLocaleString():'—']);
 }
 $('#detail').innerHTML='<p class="small">'+(p?esc(p.source_epoch||'本轮实测'):'当前配置未测')+(ad?'；adapter：'+esc(ad.source_epoch||'本轮实测'):'')+'</p>'+(isTE?'<p class="small">'+esc(teDescription(mode,a))+(ad?.mode!==teReferenceMode(mode,a)?' 当前手动选择了其他 adapter 原生配置。':'')+'</p>':'')+table(['场景','Present FPS','adapter FPS','P render/flush ms','A render/flush ms','P 非空/s','A 非空/s','P 像素/轮','A 像素/轮'],rs);
 const main=[],extra=[];
 for(const kind of ['internal','psram']){
  const metrics=r=>{
   const aligned=r?.memory_aligned,m=aligned?.memory,o=m?.after_open,b=m?.before_open,c=m?.after_close;
   return {aligned:!!aligned,total:o?.[kind+'_total_used'],reserved:o?.[kind==='internal'?'internal_static':'psram_reserved'],heap:o?o[kind+'_total']-o[kind+'_free']:null,delta:b&&o?b[kind+'_free']-o[kind+'_free']:null,open:o?.[kind+'_free'],close:b&&c?b[kind+'_free']-c[kind+'_free']:null,min:r?.memory?.after_run?.[kind+'_min_free']};
  };
  const pm=metrics(memorySample('present',mode,a,p)),am=metrics(memorySample('adapter',isTE?$('#teReference').value:mode,a,ad)),name=kind==='internal'?'SRAM':'PSRAM',alignedCompare=pm.aligned&&am.aligned;
  for(const [key,title,higher,unit,target] of [['total','Total Used After Open',false,'KiB',main],['reserved','Image / Reserved',false,'KiB',extra],['heap','Allocated Heap',false,'KiB',extra],['delta','Open Delta',false,'KiB',extra],['open','After Open Free',true,'KiB',extra],['close','After Close Delta',false,'B',extra],['min','Original Run Min Free',true,'KiB',extra]]){
   target.push([name+' '+title,memoryValue(pm[key],am[key],higher,unit,key!=='min'&&alignedCompare),memoryValue(am[key],null,higher,unit,false)]);
  }
 }
 const header=['Memory','Present','adapter'];
 $('#memory').innerHTML='<h3>Memory</h3>'+(p?.memory_reference_note?'<p class="small">'+esc(p.memory_reference_note)+'</p>':'')+(isTE?'<p class="small">'+esc(teDescription(mode,a))+' 当前 adapter：'+esc(ad?.mode?.replace('ADAPTER_TE_','')??'—')+'</p>':'')+table(header,main)+'<p class="small">独立内存补采：双方均为 24 KiB SRAM 控制任务 + 24 KiB SRAM LVGL 任务；LVGL 优先级 6、无绑核，首轮刷新前采样，完整 benchmark 代码保留。Total Used：SRAM = 链接器保留区 + 已分配堆（含 INTERNAL 堆，排除固定缓存/ROM保留及RTC静态区）；PSRAM = 物理容量 − 空闲（含XIP及映射保留）。框架自身额外任务计入。Run Min Free 在打开显示前启动局部监测，覆盖打开过程与该行完整 benchmark；使用该行性能测试的任务布局，与独立内存补采分开，不标升降；每行缓冲容量与测量批次以实测审计为准。</p><details><summary>Memory details</summary>'+table(header,extra)+'</details>';
}
function draw(){let b=DATA.boards[board];$('#title').textContent=b.label;document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('active',x.dataset.id===board));$('#summary').textContent='本轮 '+b.rows.length+' / '+b.planned+' 组 · 内存采样 '+b.memory_passed+' 组 · '+b.status.phase+'；'+(b.hardware_note||'');let modes=[...new Set(b.rows.filter(r=>r.implementation==='present'&&!r.mode.includes('TE_')).map(visibleMode).filter(Boolean))];const baseMode=m=>m.replace(/ · D[12]$/,'');const modeOrder=[...new Set(modes.map(baseMode))];modes.sort((a,b)=>modeOrder.indexOf(baseMode(a))-modeOrder.indexOf(baseMode(b))||a.localeCompare(b));if(!modes.length)modes=board.includes('rgb')||board==='p4-dsi'?['DOUBLE_FULL','TRIPLE_FULL','DOUBLE_DIRECT','DOUBLE_PARTIAL · D1','TRIPLE_PARTIAL · D1','DOUBLE_PARTIAL · D2','TRIPLE_PARTIAL · D2']:['NONE_CALLER_D1_INTERNAL','NONE_BACKGROUND_D1_INTERNAL'];let te=[...new Set(b.rows.filter(r=>r.mode.includes('TE_')).map(key))];$('#overview').innerHTML=overviewTable(modes,te,'lvgl_fps');$('#nonempty-overview').innerHTML=overviewTable(modes,te,'nonempty_fps');$('#te').hidden=!te.length;$('#teview').innerHTML=table(['实现','模式',...angles.map(a=>a+'°')],te.flatMap(m=>['present','adapter'].filter(i=>angles.some(a=>row(i,m,a))).map(i=>[i==='present'?'Present':'adapter',esc(label(m,i)),...angles.map(a=>value(row(i,m,a)))])));delete $('#teReference').dataset.manual;let previous=$('#mode').value;$('#mode').innerHTML=[...modes,...te].map(m=>'<option value="'+esc(m)+'">'+esc(label(m))+'</option>').join('');if([...$('#mode').options].some(o=>o.value===previous))$('#mode').value=previous;scene();$('#errors').innerHTML=failureView(b.status.failures);if(b.status.unsupported?.length)$('#errors').innerHTML+='<p>原生不支持的双 tile PARTIAL 项：'+b.status.unsupported.length+'，不计成绩。</p>'}
const selectedPath=document.createElement('div');selectedPath.id='selected-path';$('#angle').after(selectedPath);
$('#boards').innerHTML=Object.entries(DATA.boards).map(([id,b])=>'<button data-id="'+id+'">'+b.label+'</button>').join('');document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{board=b.dataset.id;draw()});const teRef=document.createElement('select');teRef.id='teReference';teRef.title='TE 场景对比的 adapter 原生参考配置';teRef.innerHTML='<option value="ADAPTER_TE_ASYNC_FULL">TE 参考：adapter ASYNC FULL</option><option value="ADAPTER_TE_ASYNC_BOUNCE">TE 参考：adapter ASYNC BOUNCE</option><option value="ADAPTER_TE_SYNC_FULL">TE 参考：adapter SYNC FULL</option>';$('#mode').after(teRef);teRef.onchange=()=>{teRef.dataset.manual='1';scene()};$('#mode').onchange=()=>{delete teRef.dataset.manual;scene()};$('#angle').onchange=()=>{delete teRef.dataset.manual;scene()};$('#status').textContent='本轮实测 '+Object.values(DATA.boards).reduce((n,b)=>n+b.rows.length,0)+' / 254 组 · 对齐总内存 '+Object.values(DATA.boards).reduce((n,b)=>n+b.memory_passed,0)+' 组 · '+DATA.generated;draw();

if([...$('#mode').options].some(o=>o.value==='DOUBLE_FULL'))$('#mode').value='DOUBLE_FULL';scene();

function failureView(failures){
 const pending=failures.filter(f=>!f.recovered),recovered=failures.filter(f=>f.recovered);
 const count=new Set(pending.map(f=>[f.implementation,f.mode,f.angle,f.tiles].join('/'))).size;
 const rows=items=>table(['实现','配置','角度','输入','记录'],items.map(f=>[esc(f.implementation||'—'),esc(f.mode||'—'),esc(String(f.angle??'—'))+'°','D'+esc(String(f.tiles??'—')),esc((f.detail?f.detail+' ':'')+(f.reason||'未完成'))]));
 return (pending.length?'<p>未完成配置 '+count+' 组；异常尝试 '+pending.length+' 次。</p>'+rows(pending):'<p>所有配置均已取得完整成绩。</p>')+(recovered.length?'<details><summary>已恢复的尝试：'+recovered.length+' 次</summary>'+rows(recovered)+'</details>':'');
}
