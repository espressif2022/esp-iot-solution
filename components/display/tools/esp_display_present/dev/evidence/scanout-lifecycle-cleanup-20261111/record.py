from pathlib import Path
import difflib,gzip,hashlib,json,re,shutil,subprocess,tarfile
repo=Path('/home/xuxin/xuxin/esp-iot-solution-sensor-MR')
component=repo/'components/display/tools/esp_display_present'
work=Path('/tmp/present-lifecycle-cleanup-20261111')
out=component/'dev/evidence/scanout-lifecycle-cleanup-20261111'
summary=[];tables=[]
records={}
for board in ('s3-rgb','s31-rgb','p4-dsi'):
 root=work/'hardware'/board/'benchmark/present-xip-on'
 result=json.loads((root/'results.json').read_text());manifest=json.loads((root/'manifest.json').read_text())
 assert all(result[k] for k in ('complete','matrix_complete','memory_complete','buffers_verified')) and not result['errors'],board
 assert len(result['rows'])==8 and all(x['status']=='PASS' and x['scenes']==16 for x in result['rows']),board
 for rel,h in manifest['component_sources_sha256'].items():
  assert hashlib.sha256((repo/'components/display/tools'/rel).read_bytes()).hexdigest()==h,(board,rel)
 project=json.loads((root/'build/project_description.json').read_text())
 for name in ('esp_lcd','esp_driver_ppa'):
  if name in project['build_component_info']:
   assert Path(project['build_component_info'][name]['dir']).resolve()==Path('/home/xuxin/esp_work/esp-idf-qspi/components')/name
 raw=(root/'live.log').read_text(errors='replace');active=None;counters={}
 assert not any(t in raw for t in ('CLOSE_STATE','PPA_STATE','PPA_REPLAY','PIXEL_STATE'))
 for line in raw.splitlines():
  m=re.search(r'RUN mode=(\S+).*rotation=(\d+)',line)
  if m:active=(m[1],int(m[2]))
  m=re.search(r'executor=(\w+) cpu_attempts=(\d+) hardware_attempts=(\d+)',line)
  if m and active:counters[active]={'preferred_executor':m[1],'cpu_attempts':int(m[2]),'hardware_attempts':int(m[3])}
 records[board]=(root,result,manifest,counters)
 oldroot=Path('/tmp/present-s3-no-fb-iram-20261110' if board=='s3-rgb' else '/tmp/present-xip-on-four-angle-20261110')/board/'benchmark/present-xip-on'
 old=json.loads((oldroot/'results.json').read_text());oldrows={(x['mode'],x['rotation']):x for x in old['rows']}
 for r in result['rows']:
  mem=r['memory'];past=oldrows[(r['mode'],r['rotation'])]['memory']
  delta=lambda m:m['before_open']['psram_free']-m['after_open']['psram_free']
  row=dict(board=board,mode=r['mode'],rotation=r['rotation'],lvgl_fps=r['lvgl_fps'],new_open_psram_bytes=delta(mem),historical_open_psram_bytes=delta(past),saved_open_psram_bytes=delta(past)-delta(mem),close_psram_difference_bytes=mem['after_close']['psram_free']-mem['before_open']['psram_free'],**counters[(r['mode'],r['rotation'])])
  assert row['close_psram_difference_bytes']==0,row
  summary.append(row)
  tables.append('| %s | %s | %s° | %s | %.2f | %.2f | %.2f | %d |' %(board,r['mode'],r['rotation'],r['lvgl_fps'],delta(past)/1024,delta(mem)/1024,(delta(past)-delta(mem))/1024,row['close_psram_difference_bytes']))
assert subprocess.check_output(['git','-C','/home/xuxin/esp_work/esp-idf-qspi','status','--porcelain','--untracked-files=no'],text=True)==''
out.mkdir(parents=True,exist_ok=True)
def copy_run(source,dest,extra=()):
 dest.mkdir(parents=True,exist_ok=True)
 for name in ('manifest.json','results.json','sdkconfig','bench_cases.h',*extra):
  if (source/name).is_file():shutil.copy2(source/name,dest/name)
 for name in ('live.log','build.log','subsequent-boot.log'):
  if (source/name).is_file():(dest/(name+'.gz')).write_bytes(gzip.compress((source/name).read_bytes(),mtime=0))
for board,(root,_,_,_) in records.items():copy_run(root,out/board)
for name,path in [('host-lvgl96','/tmp/present-lifecycle-host-ppa-fix.log'),('host-lvgl90','/tmp/present-lifecycle-host90-ppa-fix.log')]:
 data=Path(path).read_bytes();assert b'100% tests passed, 0 tests failed out of 48' in data
 (out/(name+'.log.gz')).write_bytes(gzip.compress(data,mtime=0))
for folder,name,extra in [
 ('failed-p4-auto90','p4-first-failure',()),
 ('p4-ppa-repair-stall','p4-ppa-repair-stall',()),
 ('ppa-replay-before-guard','ppa-replay-before-guard',('bridge_present.c','present_executor_ppa.c')),
 ('ppa-replay-after-guard','ppa-replay-after-guard',('bridge_present.c','present_executor_ppa.c')),
 ('p4-passive-startup-fault','p4-observer-startup-fault',()),
]:copy_run(work/folder,out/'diagnostics'/name,extra)
# Recover the exact normal-benchmark bridge used for the canonical copy stall.
bridge=(work/'ppa-replay-before-guard/bridge_present.c').read_text()
for include in ('pixel/present_executor.h','esp_lcd_mipi_dsi.h','esp_lcd_panel_ops.h'):
 bridge=bridge.replace('#include "'+include+'"\n','')
a=bridge.index('esp_err_t bench_init(void)');b=bridge.index('esp_err_t bench_open',a)
bridge=bridge[:a]+'''esp_err_t bench_init(void)
{
    return hw_lcd_init(&panel, &io, 3, HW_ROTATE_0);
}
'''+bridge[b:]
m=json.loads((work/'p4-ppa-repair-stall/manifest.json').read_text())
assert hashlib.sha256(bridge.encode()).hexdigest()==m['test_sources_sha256']['test_apps/benchmark/main/bridge_present.c']
(out/'diagnostics/p4-ppa-repair-stall/bridge_present.c').write_text(bridge)
archive=work/'diagnostic-source.tar.gz'
with tarfile.open(archive) as tf:
 assert all('/build/' not in x.name and not x.name.endswith(('.bin','.elf','.o','.a')) for x in tf.getmembers())
shutil.copy2(archive,out/'diagnostics/replay-source.tar.gz')
two=work/'hardware-two-fb/s3-rgb/benchmark/present-xip-on'
r=json.loads((two/'results.json').read_text());assert r['complete'] and r['matrix_complete'] and not r['errors'] and len(r['rows'])==1 and r['rows'][0]['scenes']==16
copy_run(two,out/'s3-two-fb-auto',('probe-observation.json',))
(out/'s3-two-fb-auto/capture-and-report.py').write_bytes(Path('/tmp/present-capture-two-fb.py').read_bytes())
rel='components/display/tools/esp_display_present/dev/test_apps/benchmark/main/bridge_present.c'
a=(repo/rel).read_text();b=(work/'two-fb-source'/rel).read_text()
(out/'s3-two-fb-auto/driver-fb-count.patch').write_text(''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel)))
(out/'review.patch.gz').write_bytes(gzip.compress(subprocess.check_output(['git','diff','143480fb8','aadc0d799','--','components/display/tools/esp_display_present','components/display/tools/esp_lv_present'],cwd=repo),mtime=0))
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
p4=next(x for x in summary if x['board']=='p4-dsi' and x['mode']=='AUTO' and x['rotation']==90)
(out/'README.md').write_text('''# Scanout ownership and PPA admission — 2026-11-11

本轮结论：移除 FULL／原生 DIRECT 的全屏占位画布，以及 RGB/MIPI 的 NONE 路径；AUTO 按实际 3／2 张 FB 选择防撕裂 PARTIAL，单 FB 拒绝。GRAM/TE 不变。S3、S31、P4 的相关模式打开 PSRAM 增量分别减少约 **456／752／1216 KiB**；旋转 DIRECT 保留实际画布。

最终三板每块连续完整跑完 8 项，共 **24/24、384 场景**，无拼接，关闭后 PSRAM 均回到打开前。Host 两个 LVGL 版本各 48 项通过。额外定位了 P4 PPA `131×352` 修补复制挂起，并在 probe 阶段规避；**基础 IDF 和最终固件均无诊断补丁**。失败、重放和最终正式回归分开存放。

Implementation commits: `821923845` (scanout/lifecycle), `aadc0d799` (P4 PPA FIFO boundary). Base: `143480fb8`.

## Final behavior

- RGB/MIPI `NONE` and `present_lcd_fb_update` are removed. GRAM/TE NONE/AUTO are unchanged.
- AUTO resolves once against the actual driver FB pool: 3 → TRIPLE_PARTIAL, 2 → DOUBLE_PARTIAL, 1 → unsupported. The pre-panel static query recommends 3 when no AUTO count hint exists.
- LVGL FULL/native 0° DIRECT borrow writable storage before mounting/refresh admission; pixel references are revoked before submit/cancel. No full-screen placeholder allocation. Rotated DIRECT retains its actual canvas; pooled PARTIAL retains its tile-sized error fallback. Binding control and LVGL access remain UI-thread serialized.
- P4 PPA probe rejects multi-unit requests whose final macro-block payload equals the 192-byte FIFO boundary. Existing CPU fallback runs before hardware submission. No retry after acceptance, no IDF patch, no added framebuffer or public setting. The guard uses the compiled P4 SRM macro-block size (16/32); other targets keep their existing probes.

## Validation scope

Host ASan/UBSan: **48/48 on LVGL 9.6 and 48/48 on 9.0**. Includes no-canvas allocation, close before first refresh, DIRECT pixel retention/async ownership, runtime 0↔90 rotation, timeout safety, RGB/DSI 1/2/3-FB policy, and PPA geometry admission/CPU pixel oracle for RGB565/RGB888 and both macro-block sizes. The deleted update module's test was removed; a 16-row PPA variant was added.

The final main matrix is a fresh, continuous **8-case suite per board**: DOUBLE_FULL, TRIPLE_FULL, DOUBLE_DIRECT, AUTO × 0°/90°. Every case completes all 16 original LVGL scenes and the close/error checks: **24 cases / 384 scenes**, with no stitching or reuse of failed attempts. XIP ON; S3 bounce=10 lines; S31 non-bounce; clean IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`. Production component source hashes match the final checkout; IDF component paths are checked. No observer code in these final runs; no original firmware restore. All 24 cases recover PSRAM to their before-open baseline after close.

This is targeted lifecycle regression, not a new all-mode/four-angle adapter comparison. Historical performance webpages remain unchanged. Serial completion does not independently prove optical tear freedom or rule out every driver underflow.

Additional `s3-two-fb-auto/`: an earlier complete 16-scene S3 AUTO 0° run uses exactly two native FBs and validates real fallback. Its implementation is `821923845`, before the P4-only guard. The stock IDF getter emits one expected INVALID_ARG while probing three FBs on the two-FB panel; the raw log is unchanged, and only that exact diagnostic is separated from hardware errors in the archived capture/report script. One-FB rejection is host-tested.

## P4 failure found during regression

Before the PPA guard, an eight-case P4 sequence completed seven cases; AUTO 90° then completed the benchmark timers but timed out on close. Its FPS is **not a valid rendering result**: the binding had already latched an acquire timeout, so scene timers could continue while drawing paused. Failed/diagnostic attempts are outside the main table.

A passive snapshot located the accepted frame in FB_SLOT_REPAIR, one incomplete copy ticket, no pending panel transaction, and a pixel worker inside `ppa_do_scale_rotate_mirror()`. The blocked request is PSRAM FB→FB, RGB565, no rotation, source/destination (466,248), region 131×352, pitch 2048, full surfaces 1024×600. Copies and scanner ownership remained retained; the diagnostic does not release buffers or repair the driver.

Clean-IDF direct replay, independent of LVGL/FB state advancement:

| Requested width × height | Before guard | After guard |
|---|---|---|
| 130×352 | PPA returns ESP_OK | PPA returns ESP_OK |
| 132×352 | PPA returns ESP_OK | PPA returns ESP_OK |
| 131×352 | PPA blocks; no return in capture | CPU fallback returns ESP_OK |

The local IDF DIG-734 workaround uses a strict `<` comparison against a 12×128-bit FIFO. With the compiled rev3 32-row macro block, this request's tail is `(131 % 64) × 32 × 16 = 1536 bits`, exactly equal. The stalled API and adjacent-width controls are measured; attributing the class to the equality gap is based on that driver condition, not an internal FIFO trace. The presenter conservatively rejects this boundary in its pure probe. No IDF workaround predicate was patched.

`diagnostics/replay-source.tar.gz` reproduces the unguarded direct replay source, including isolated observers; its component/test hashes were verified against the archived manifest. Override its bridge with `diagnostics/p4-ppa-repair-stall/bridge_present.c` and the corresponding cases to reproduce the observed pipeline variant. Before/after replay snapshots carry their actual bridge/PPA sources. The replay capture timeouts are diagnostic stop conditions, not benchmark passes. One observed startup exception before app_main in a temporary observer image and its subsequent normal boot are retained separately; they are not main-matrix data or attributed to this component fix.

Final P4 AUTO 90° executor counters: **CPU attempts={CPU}, hardware attempts={HW}**. These are executed attempts, not exclusively guard hits; the direct replay proves the guarded selection separately.

## FPS interpretation

Borrowing now happens at REFR_REQUEST. This moves admission relative to LVGL refresh/animation accounting, so internal LVGL FPS and committed throughput should be read together. Across the historical and final targeted builds, S3 DIRECT 0° is LVGL 43→36 but transport 32.3→31.6 FPS; P4 DIRECT 0° is 42→37 and transport 32.0→30.8 FPS. The changed admission timing is a possible contributor to the metric difference, not a controlled causal proof. This round does not establish a speedup or statistically characterize performance variation. Raw results contain both metrics; no adapter rerun is claimed.

## Open PSRAM delta

Historical S3 comparison: no-FB-IRAM 20261110; S31/P4: four-angle XIP-ON 20261110. Values measure complete-firmware before/after-open heap differences, not component-exclusive memory. Native three-FB pools are allocated before open in both rounds. Positive saved means less open allocation. `close−before` is remaining PSRAM after close minus before open, in bytes. FPS belongs only to this final targeted round.

| Board | Mode | Angle | New LVGL FPS | Historical open KiB | New open KiB | Saved KiB | close−before B |
|---|---|---:|---:|---:|---:|---:|---:|
'''.replace('{CPU}',str(p4['cpu_attempts'])).replace('{HW}',str(p4['hardware_attempts']))+'\n'.join(tables)+'\n')
(out/'record.py').write_bytes(Path(__file__).read_bytes())
(out/'sha256.json').write_text(json.dumps({str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.rglob('*')) if p.is_file() and p.name!='sha256.json'},indent=2)+'\n')
print(out)
print('P4 final AUTO 90:',p4)
