# Scanout ownership and PPA admission — 2026-11-11

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

Final P4 AUTO 90° executor counters: **CPU attempts=152, hardware attempts=39572**. These are executed attempts, not exclusively guard hits; the direct replay proves the guarded selection separately.

## FPS interpretation

Borrowing now happens at REFR_REQUEST. This moves admission relative to LVGL refresh/animation accounting, so internal LVGL FPS and committed throughput should be read together. Across the historical and final targeted builds, S3 DIRECT 0° is LVGL 43→36 but transport 32.3→31.6 FPS; P4 DIRECT 0° is 42→37 and transport 32.0→30.8 FPS. The changed admission timing is a possible contributor to the metric difference, not a controlled causal proof. This round does not establish a speedup or statistically characterize performance variation. Raw results contain both metrics; no adapter rerun is claimed.

## Open PSRAM delta

Historical S3 comparison: no-FB-IRAM 20261110; S31/P4: four-angle XIP-ON 20261110. Values measure complete-firmware before/after-open heap differences, not component-exclusive memory. Native three-FB pools are allocated before open in both rounds. Positive saved means less open allocation. `close−before` is remaining PSRAM after close minus before open, in bytes. FPS belongs only to this final targeted round.

| Board | Mode | Angle | New LVGL FPS | Historical open KiB | New open KiB | Saved KiB | close−before B |
|---|---|---:|---:|---:|---:|---:|---:|
| s3-rgb | DOUBLE_FULL | 0° | 25 | 456.03 | 0.03 | 456.00 | 0 |
| s3-rgb | DOUBLE_FULL | 90° | 16 | 906.04 | 450.03 | 456.00 | 0 |
| s3-rgb | TRIPLE_FULL | 0° | 33 | 456.03 | 0.03 | 456.00 | 0 |
| s3-rgb | TRIPLE_FULL | 90° | 17 | 906.04 | 450.03 | 456.00 | 0 |
| s3-rgb | DOUBLE_DIRECT | 0° | 36 | 456.03 | 0.03 | 456.00 | 0 |
| s3-rgb | DOUBLE_DIRECT | 90° | 30 | 906.04 | 906.04 | 0.00 | 0 |
| s3-rgb | AUTO | 0° | 46 | 47.03 | 47.03 | 0.00 | 0 |
| s3-rgb | AUTO | 90° | 43 | 47.03 | 47.03 | 0.00 | 0 |
| s31-rgb | DOUBLE_FULL | 0° | 33 | 753.00 | 1.00 | 752.00 | 0 |
| s31-rgb | DOUBLE_FULL | 90° | 23 | 1503.01 | 751.00 | 752.00 | 0 |
| s31-rgb | TRIPLE_FULL | 0° | 37 | 753.00 | 1.00 | 752.00 | 0 |
| s31-rgb | TRIPLE_FULL | 90° | 32 | 1503.01 | 751.00 | 752.00 | 0 |
| s31-rgb | DOUBLE_DIRECT | 0° | 34 | 753.00 | 1.00 | 752.00 | 0 |
| s31-rgb | DOUBLE_DIRECT | 90° | 34 | 1503.01 | 1503.01 | 0.00 | 0 |
| s31-rgb | AUTO | 0° | 40 | 81.00 | 81.00 | 0.00 | 0 |
| s31-rgb | AUTO | 90° | 41 | 48.00 | 48.00 | 0.00 | 0 |
| p4-dsi | DOUBLE_FULL | 0° | 25 | 1217.01 | 1.00 | 1216.00 | 0 |
| p4-dsi | DOUBLE_FULL | 90° | 21 | 2417.01 | 1201.01 | 1216.00 | 0 |
| p4-dsi | TRIPLE_FULL | 0° | 34 | 1217.01 | 1.00 | 1216.00 | 0 |
| p4-dsi | TRIPLE_FULL | 90° | 24 | 2417.01 | 1201.01 | 1216.00 | 0 |
| p4-dsi | DOUBLE_DIRECT | 0° | 37 | 1217.01 | 1.00 | 1216.00 | 0 |
| p4-dsi | DOUBLE_DIRECT | 90° | 30 | 2417.01 | 2417.01 | 0.00 | 0 |
| p4-dsi | AUTO | 0° | 53 | 103.01 | 103.01 | 0.00 | 0 |
| p4-dsi | AUTO | 90° | 51 | 60.01 | 60.01 | 0.00 | 0 |
