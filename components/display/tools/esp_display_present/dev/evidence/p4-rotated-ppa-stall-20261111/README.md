# P4 rotated PPA stall investigation — 2026-11-11

Production base: 4a68ee87b (runtime aadc0d799). ESP-IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe, no tracked driver modifications. P4 MAC e8:f6:0a:e7:a7:8d, rev3.2, 1024×600 MIPI DSI, XIP on.

## Observed first failure

Targeted continuous run: DOUBLE_DIRECT270 PASS, DOUBLE_PARTIAL90 PASS, DOUBLE_PARTIAL270 stopped at tick159743 with a binding TIMEOUT. Snapshot showed no pending panel transaction, active FB1 DRAWING, FB0 SCANNING, tile ticket114651 RUNNING. PPA stage3 is inside the blocking ppa_do_scale_rotate_mirror() call.

Request: RGB565, source internal SRAM 0x4ff491c0, area(0,0,128,130), stride256, declared capacity60000; destination PSRAM FB0x4821ca80, 1024×600 stride2048 capacity1228800, destination(727,353), rotation270.

At close tick221641 the same request was still RUNNING; cancellation was pending. bench_close ultimately reported INVALID_STATE. This demonstrates that at least one INVALID_STATE close result was secondary to a PPA stall, rather than a separate scanout exchange failure. Other historical failures must not be assigned the same first fault without evidence.

## Independent reproduction

Remove LVGL and present frame lifecycle; call the PPA executor directly on the same geometry, alternating source heights129,131,130. Single pass of three requests returned normally. Repeated run stalled at iteration17 (18th call), height130, stage3. Watchdog snapshot retained previous17 requests. No IDF patch, no display framebuffer exchange by present.

IDF's DIG-734 workaround chooses macro-block-order bypass from unrotated input geometry. For128×130 RGB565 with32-row blocks, input tail64×2×2=256bytes leaves bypass disabled. Rotated output130×128 has tail2×32×2=128bytes. This geometry mismatch is consistent with the known macro-block-order stall; FIFO internals were not traced and an IDF workaround patch was not applied to establish register-level causality.

## Component correction

Retain the previously independently reproduced input-tail192-byte equality exclusion. Additionally, for quarter turns whose input predicate does not enable IDF bypass, reject output tails≤192bytes across multiple transfer units. Rejection occurs in the pure executor probe, before any cache maintenance, descriptor submission or hardware acceptance. Existing dispatcher uses CPU fallback for that request. Other supported geometries continue using PPA. No public configuration, resident buffers, tasks, per-frame logging, or changes to S3/S31 runtime paths.

After correction:20000 alternating requests completed, whole1228800-byte destination matched independent CPU output, including pixels outside the copied region. Sanitized host44/44 PASS; tests cover RGB565/RGB888,90/270°, macro16/32, pure rejection and preserved hardware eligibility of safe neighbors.

## Adapter history cross-check

- f4116b61a (in current HEAD): optional IDF patch unconditionally sets ppa_ll_srm_bypass_mb_order(true); README documents P4 partial+rotation freeze. This patch is distributed by adapter, not automatically applied, and has NOT been applied in this investigation.
- b5682317d (in HEAD): per-buffer cache line size and aligned M2C invalidation.
- 59bf442c1 (in HEAD): encrypted/ECC external-memory DMA window alignment, including base/stride/x/width and PPA CPU fallback.
- 374c6cb2a (in HEAD): DMA2D API compatibility.
- 9539736b7 (in HEAD):128-byte DMA burst optimization. Present currently keeps its own driver default; this is a separate performance question, not evidence for this stall.
- 682d62441,7e37982d8,5e7dfad1a,46a863af8 are reachable in local history but NOT ancestors of current HEAD. Relevant later fixes include true allocated output capacity/alignment and14-bit DMA2D dimensions. Present already checks caller-declared surface capacity, complete cache bands, per-buffer cache constraints,14-bit dimensions, and encrypted DMA windows. These later commits were reviewed, not cherry-picked or substituted into the adapter comparison baseline.

Current scope does not claim the driver observer proved absence of every optical display artifact. No blanket reclassification of historical failed records, nor broad matrix reruns.

## Formal corrected-code regression

All five unique historical failing combinations completed16scenes and closed successfully: DOUBLE_DIRECT270, DOUBLE_PARTIAL90/270, TRIPLE_PARTIAL90, AUTO270. This is80complete scenes. Each case restored PSRAM free bytes exactly to its own before-open value. Raw regression logs and manifest verify runtime source hashes against the repository. Original firmware was not restored; board retains corrected test firmware. Historical comparison data are not silently replaced by this targeted regression.
