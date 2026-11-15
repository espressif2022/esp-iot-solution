# Validation summary

This file summarizes evidence; it is not the complete measurement dataset.
The 2026-11-11 board results retain their tested revisions/configuration reuse.
They are not a fresh hardware run of this MR's later cleanup commits.

## Code checks

| Check | Result |
|---|---|
| Host / ASan+UBSan | 44/44 / 44/44 |
| LVGL 9.5 binding / ASan+UBSan | 48/48 / 48/48 |
| Clean-branch Host and repository hooks | 44/44; hooks passed |
| P4 / S31 smoke, local IDF `80c8922480b5` | Compiled, not flashed |
| Exact IDF v6.0 S3 / P4 smoke | Compiled, not flashed; P4 uses CPU fallback, DSI adapter unavailable on this SDK |
| Public P4 `lvgl_present_benchmark`, IDF v6.0 | Dependency resolution and build passed in isolated copy; not flashed, no DSI runtime support claim |
| P4 smoke, local IDF `80c8922480b5`, after SDK API detection | Compiled with both accelerators and DPI callback support enabled; not flashed |

The minimum SDK compile check uses exact IDF v6.0 sources/submodule revisions,
not an arbitrary local 6.x development checkout. See [compatibility](../COMPATIBILITY.md):
CPU/core availability does not imply optional accelerator or DSI adapter availability.
Host and smoke checks do not establish optical tear freedom.

GitLab CI runs Host tests with and without ASan/UBSan, plus the LVGL 9.5.0 binding
in both variants. LVGL is pinned to commit
`85aa60d18b3d5e5588d7b247abf90198f07c8a63`. Smoke build jobs use the maintained
`dev/test_apps/smoke` entry on S3/P4, with IDF 6.0/6.1/latest. CI configuration
validation and local checks are not a claim that remote jobs have passed.

## Host GSP ↔ LVGL handoff

| Dependency | Version used |
|---|---|
| Present / LVGL binding | Workspace at base `1bb3ad36` with the current changes, manifests `2.0.0` / `0.2.0` |
| GSP core and split adapter | `esp-gsp` / `esp_gsp_adapter` `1.2.0`, source commit `0a40cc7207850c3ce783f224f476d0fbdc354097` |
| LVGL | Registry sources `9.2.2` (copy) and `9.5.0` (pooled RGB565 PARTITION) |
| Host | Linux x86-64, GCC 11.4.0, CMake 3.22.1 |

Both LVGL configurations passed all 51 Host tests. LVGL 9.5.0 also passed
51/51 under ASan/UBSan with `ASAN_OPTIONS=detect_leaks=0` (LeakSanitizer
cannot run in this environment). The integration passed 50 consecutive runs
with LVGL 9.5.0.

The optional `gsp_lvgl_integration` test builds the GSP LCD adapter, its Present
output and its ESP-IDF pause/resume state machine from source. The GSP worker
runs on a POSIX thread. The fixture uses the checkout's `nav.gspb`, a Host
FreeRTOS shim and a mocked panel IO that completes transfers. GSP hardware
acceleration and background decoding are disabled. The first resume fails
after a real Present write lease is acquired. The test checks cancellation,
the original GSP task ownership, the same token's single deallocation on
success, and the restored GSP frame at the panel boundary.

From the Present component directory, run with the local dependency source trees.
Repeat with the other LVGL version to exercise both paths. GSP's portable
build uses Host zlib, JPEG and FreeType development packages.

```sh
python3 dev/tools/dev.py test --lvgl-dir /path/to/lvgl --gsp-dir /path/to/esp-gsp
```

The default Host CI suite has no GSP source checkout and runs the standalone
handoff-stage regression. The optional integration uses the split adapter source
above, directly against the Present 2.0 headers. The split adapter's manifest
still declares Present `^1.0.2`.

The ESP32-S3 handoff example also compiled and linked in an offline copy with
the Registry `esp-gsp` 1.2.0 prebuilt archive, LVGL 9.5.0, GSPC 0.3.0 and
ESP-IDF 6.2 development commit `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`.
That build used local copies of the cached dependencies and ran on no hardware.

## 2026-11-15 RGB performance

On-board runs used the ESP32-S3 Function EV board with GC9503 480×480 RGB565,
8 MiB PSRAM and ten bounce lines, and the ESP32-S31 Korvo-1 with stock
800×480 RGB565 at 18 MHz in non-bounce mode. Runs used ESP-IDF commit
`fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`, LVGL Registry source 9.5.0,
two LVGL draw units, zero rotation and all 16 scenes. The two baseline suites
and their tuning variants passed 20 cases and 320 scenes, including buffer
audits and cleanup, with zero buffer-wait timeouts.

| Board | Mode | PSRAM XIP | Draw tiles | Commit FPS | LVGL FPS | Begin-to-commit P99 (ms) | Buffer waits | Missed supply intervals |
|---|---|---|---:|---:|---:|---:|---:|---:|
| S3 | DOUBLE_FULL | On | — | 21.6 | 24 | 77 | 1,430 | 0 |
| S3 | TRIPLE_FULL | On | — | 28.1 | 32 | 77 | 0 | 0 |
| S3 | DOUBLE_DIRECT | Off → On | — | 23.5 → 28.9 | 27 → 34 | 92 → 69 | 1,452 → 1,768 | 0 → 0 |
| S3 | TRIPLE_PARTIAL | Off → On | 1 | 22.0 → 30.6 | 31 → 45 | 139.8 → 96 | 7,227 → 10,656 | 217 → 10 |
| S3 | TRIPLE_PARTIAL | On | 2 | 32.0 | 47 | 90 | 2,646 | 11 |
| S31 | TRIPLE_FULL | Off | — | 26.8 | 30 | 92 | 493 | 0 |
| S31 | TRIPLE_FULL | On | — | 32.9 | 36 | 59 | 1,239 | 0 |
| S31 | DOUBLE_DIRECT | Off → On | — | 23.0 → 30.7 | 26 → 34 | 90 → 53 | 1,558 → 2,045 | 0 → 0 |
| S31 | TRIPLE_PARTIAL | Off → On | 1 | 22.2 → 30.8 | 29 → 40 | 181.6 → 75 | 8,781 → 14,735 | 0 → 0 |
| S31 | TRIPLE_PARTIAL | Off | 2 | 22.1 | 29 | 182.2 | 1,558 | 0 |
| S31 | TRIPLE_PARTIAL | On | 2 | 31.7 | 40 | 72 | 2,776 | 0 |

Commit FPS counts frames accepted by Present per elapsed second. LVGL FPS is
the mean across the benchmark scenes. P99 comes from the frame-duration
histogram and reports the maximum when the percentile falls in its overflow
bucket. Missed supply intervals require backlog at the previous VSYNC.

The second S3 tile reduced buffer waits by 75% and the minimum free internal
heap from 135,183 to 84,907 bytes. On S31, XIP with TRIPLE_FULL gave the
highest commit throughput, and XIP with DOUBLE_DIRECT gave the lowest
P99. Adding the second S31 PARTIAL tile with XIP on improved throughput
from 30.8 to 31.7 commit FPS. It reduced the minimum free internal heap
from 221,243 to 141,139 bytes. The S31 SDK used the CPU Present copy path
because the required private accelerator APIs were unavailable.

Reproduce the single-variable trials with `dev/tools/run.py build --board
s3-rgb --angles 0 --profiles TRIPLE_PARTIAL --draw-buffers 2 --lvgl-dir
/path/to/lvgl-9.5.0`. Use `--xip off` on S3 or `--xip on` on S31 to
compare against the respective default XIP setting. The runner records
the LVGL source version in the build manifest and per-mode metrics in
`results.json`. Board runs verify the transfer, buffer audit and cleanup
through serial logs. Optical quality still requires a view of the screen.
Both boards were restored to their original firmware and verified against
the full 16 MiB flash backups.

## Performance path regressions

- `NONE` with native byte order and a tight stride submits the original draw
  lease after PSRAM falls back to internal DMA memory. Host tests check the
  early callback, delayed callbacks through stop timeout, a padded-stride
  repack, and automatic T2-to-T1 retry when PSRAM is lost between attempts.
- S31 RGB Host tests count one cache writeback per frame in the audited
  direct RGB path and two in the conservative direct RGB path. Bounce and
  DPI keep the adapter writeback. The fallback rejects a writeback error
  before driver submission. Early link completion and submit errors are
  covered in both paths.
- Native FULL starts with one backend worker and saves a 4 KiB pixel stack.
  Host tests inject both logical-canvas and worker allocation failures during
  rotation, submit again at the original angle, and verify the rotated
  pixels after retry.

Host with LVGL 9.5.0 and GSP passed 51/51 both normally and with ASan/UBSan
(`ASAN_OPTIONS=detect_leaks=0`). The S3 and S31 RGB benchmark firmware built
offline with LVGL 9.5.0 and ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`.
The profiled build used XIP on and `DOUBLE_FULL,TRIPLE_FULL` at 0° and 90°.

## One-call setup and RGB board verification

The public `open()` and `open_ex()` paths use the same resource selection as
`create()`. The Host facade covers all modes, AUTO on 1/2/3 scanout buffers,
physical-to-logical axes, explicit T1/T2, and all four TE tile/compose
combinations. The LVGL 9.5.0 and GSP Host suite passed 51/51 in both the
regular and ASan/UBSan configurations.

On the S3 Function EV 480×480 with 8 MiB PSRAM and S31 Korvo-1 800×480,
the public pixel matrix passed 24 cases and 96 frames per board. It used
all six RGB mode selections and four creation angles. Each case checked
the draw contract, effective mode, FB and tile counts, and logical and
physical strides, then repeated the checks after a runtime half-turn.
Every pixel submitted to the RGB driver was checked against a reference.
This covers the driver input and completion callbacks. Optical output
requires a separate observation. Panel IO and the TE combinations above
were covered on Host.

All performance rows used ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`,
LVGL 9.5.0, RGB565, XIP on, 0° and 16 scenes per case. The S31 profiled
A/B at 18 MHz with no bounce changed only the private BSP writeback
selection, with the original Present pre-submit C2M enabled in the
comparison firmware. Present and LVGL source digests, effective SDK
configuration and generated case table matched.

| Board and case | Commit FPS | Begin-to-commit P99 | Buffer waits | Wait timeouts |
|---|---:|---:|---:|---:|
| S31 TRIPLE_FULL, comparison | 33.0 | 59 ms | 1,232 | 0 |
| S31 TRIPLE_FULL, driver writeback | 33.0 | 58 ms | 1,248 | 0 |
| S3 TRIPLE_FULL | 28.3 | 77 ms | 0 | 0 |
| S3 DOUBLE_DIRECT | 29.2 | 69 ms | 1,791 | 0 |

S31 went from 4,576 C2M calls for 2,288 panel submissions to 2,291 for
2,291 submissions. C2M time per submission went from 248.7 to 239.3 µs,
a 3.8% reduction in the measured writeback stage. The requested cache
range per submission fell from 1,536,000 to 768,000 bytes. Commit throughput
was unchanged in this comparison. All four runs passed buffer-size and
allocation audits and reported zero missed VSYNC supply intervals.

Reproduce the S31 profile with `dev/tools/run.py build --board s31-rgb
--profile --xip on --angles 0 --profiles TRIPLE_FULL --lvgl-dir
/path/to/lvgl-9.5.0`. For the comparison, build from a temporary source
copy with `no_explicit_restart = false` in
`dev/test_apps/boards/main/board_rgb_info.c`. For the pixel checks, use
`--suite pixels` on the board under test. The attached S3 Function EV
was identified by MAC `7c:df:a1:fd:c6:04` and uses the S3 RGB firmware
profile. The original flash was restored on both boards and verified
against the full 16 MiB backup.

## S31 TRIPLE_FULL render bottleneck

The 800×480 Korvo RGB panel ran at 18 MHz without bounce, using RGB565,
three scanout buffers, 0° rotation, XIP on, two LVGL software draw units,
LVGL 9.5.0 and ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`.
The scanout geometry gives a ceiling of 42.15 Hz, also seen in 2,926 VSYNCs
over about 69.4 seconds. Lightweight scenes reached 42 FPS.

In the two-stripe baseline, LVGL's render-start to render-ready interval
totaled 53.0 seconds of the 69.4-second suite. Draw-buffer waits totaled
8.52 seconds. One panel submission averaged about 0.26 ms, including its
cache writeback. The native FULL path performed zero executor copies.
Screen-sized text and opacity layers were rendering-limited. The profiled
intervals can nest, so the panel time is included in the render interval
when the submission occurs during flush.

| LVGL software stripes | Commit FPS | Begin-to-commit P99 |
|---:|---:|---:|
| 2 | 33.1 | 58 ms |
| 4 | 33.0 | 59 ms |
| 6 | 32.6 | 63 ms |
| 8 | 32.3 | 65 ms |

The stripe trials reused one firmware with only the LVGL stripe count
changed at run time. Two stripes remain the default. A separate
same-configuration A/B changed the RGB565 and ARGB8888 software blend
sources from `-O2` to `-O3`. Commit FPS changed from 32.9 to 32.7 and
P99 stayed at 59 ms. Two 800×480 images containing rounded corners, a
translucent layer, shadow, text and arcs had matching full-frame CRC32
values. The compile override was removed.

Keeping eligible ARGB8888 intermediate buffers in internal RAM improved
the opacity-layer scene from 22 to 30 FPS and reduced its render time
from 41 to 28 ms. Across all 16 scenes it moved commit FPS from 32.9 to
33.0, with the same 59 ms P99. Minimum free internal RAM fell from
271 to 124 KiB. The allocator experiment stayed in the test firmware.

The implementation accelerates large opaque RGB565 whole-row fills
inside the current PSRAM write lease. It checks the destination range,
row alignment and cache line size. PPA completion and cache invalidation
finish before LVGL can use the filled rows. A driver or cache error is
reported to the flush path so the frame is cancelled. Software handles
smaller, masked or non-eligible fills.

The final A/B used the same generated case, benchmark source, SDK
configuration, scanout timing, LVGL version and 16 scenes.

| Metric | Baseline | Lease-checked PPA fills |
|---|---:|---:|
| Commit FPS | 32.9 | 35.3 |
| Begin-to-commit P99 | 59 ms | 53 ms |
| Mean render interval per frame | 23.48 ms | 17.59 ms |
| Buffer waits | 1,224 | 1,508 |
| Buffer wait timeouts | 0 | 0 |
| Screen-sized text scene FPS | 31 | 41 |
| Widgets scene FPS | 19 | 22 |

Both captured RGB565 reference frames matched the baseline CRC32 values
`33e96f90` and `65a6e01d`. The runs passed the buffer audit and logged no
LCD underruns, missed supply intervals or runtime faults. The new Host
test covers invalid leases, out-of-range rows, unsupported alignment,
and errors from PPA and output cache synchronization. LVGL 9.5.0 Host
passed 52/52 both normally and with ASan/UBSan, and LVGL 9.2.2 passed 51/51.

Use `dev/tools/run.py build --board s31-rgb --profile --xip on --angles 0
--profiles TRIPLE_FULL --lvgl-dir /path/to/lvgl-9.5.0` to collect
`stage=render` alongside the Present stages.

## 2026-11-15 all-mode performance review

The current profiled firmware uses ESP-IDF
`fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`, LVGL 9.5.0 with two software
draw units, RGB565 and 16 scenes per case. The RGB comparison uses
0° creation rotation, native byte order, PSRAM XIP on, the same physical
three-FB panel pool and 50-line PARTIAL tiles. The GC9503 S3 Function EV
board is 480×480 with 10 bounce lines. The S31 Korvo is 800×480 at 18 MHz
with direct scanout.

FPS counts frames accepted by Present. P99 starts when begin succeeds
and ends at commit. The time waiting inside begin is part of the buffer
waiting metric, and scanout or bus completion can happen after commit.
The buffer waiting figure sums waits and divides by the number of
sampled frames. A supply miss is a VSYNC interval that started with a
pending frame but did not switch buffers. Optical results require an
observer or external capture.

| Mode | Tiles | S3 FPS | P99 (ms) | Wait (ms/frame) | Supply misses | Korvo FPS | P99 (ms) | Wait (ms/frame) | Supply misses |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| DOUBLE_FULL | — | 21.5 | 79 | 9.1 | 0 | 28.8 | 60 | 8.9 | 0 |
| TRIPLE_FULL | — | 28.1 | 78 | 0.0 | 0 | 35.1 | 53 | 6.8 | 0 |
| DOUBLE_DIRECT | — | 29.1 | 70 | 6.9 | 0 | 30.7 | 56 | 11.1 | 0 |
| DOUBLE_PARTIAL | 1 | 27.0 | 96 | 11.0 | 8 | 28.3 | 79 | 13.9 | 0 |
| DOUBLE_PARTIAL | 2 | 29.0 | 89 | 7.1 | 5 | 28.4 | 78 | 11.0 | 0 |
| TRIPLE_PARTIAL | 1 | 30.4 | 95 | 7.3 | 7 | 30.5 | 79 | 10.7 | 1 |
| TRIPLE_PARTIAL | 2 | 31.8 | 88 | 3.7 | 9 | 31.1 | 78 | 6.9 | 0 |
| AUTO | 1 | 30.5 | 94 | 7.5 | 11 | 30.6 | 79 | 10.6 | 0 |
| AUTO | 2 | 31.7 | 90 | 3.7 | 13 | 31.1 | 78 | 7.1 | 0 |

All 18 measured rows completed 16 scenes, passed the buffer audit and
closed with zero buffer-wait timeouts. The S3 observed about 4,185 VSYNCs
per row and the Korvo about 2,927. On S3, T2 reduced waiting for
`TRIPLE_PARTIAL` by about half, with 51,028 fewer bytes of minimum free
internal heap. `DOUBLE_DIRECT` had the lowest P99 among the S3 rows.

Korvo T2 rows each opened on a cold boot. In the sequential baseline,
`TRIPLE_PARTIAL` returned `ESP_ERR_NO_MEM` after closing a successfully
measured `DOUBLE_PARTIAL` case. The benchmark recorded 386,547 bytes of
free internal heap but a largest internal DMA block of 139,264 bytes,
down from 327,680 bytes at cold boot. Each explicitly selected 50-line
tile requires one aligned 80,000-byte allocation. The same T2
`TRIPLE_PARTIAL` and `AUTO` finished in isolated boots. The sequential
reopen fix is covered below.

The S31 native FULL and DIRECT cases submit the leased driver FB. On
`DOUBLE_DIRECT`, LVGL also synchronizes dirty regions between FBs. The
single-tile PARTIAL cases issued about seven buffer waits per committed
frame and recorded about 6 ms of Present CPU copy time per frame. This
SDK build lacks Present's DMA2D alignment and async interfaces. S31's
LVGL whole-row PPA fills improve `TRIPLE_FULL` and run independently of
the Present copy profile.

Rotation, byte order and memory placement change the execution path.
Rotated or byte-swapped FULL/DIRECT draw into a logical canvas before
conversion into a scanout FB. PARTIAL copies and converts as it assembles
the FB. Panel IO `NONE` can swap compact internal RGB565 tiles in place
at 0°; PSRAM tiles use internal TX staging. `TE_SYNC` composes an entire
frame and gates its transfer on TE. T2 adds draw/copy overlap and C2 adds
composition/transfer overlap. Link clock and TE timing set the transfer
budget. For AUTO, the Host tests verify two-FB RGB/DPI
`DOUBLE_PARTIAL`, the one-FB unsupported case and both Panel IO choices.
The three-FB RGB benchmark uses `TRIPLE_PARTIAL`. The pixel matrix above
covers RGB rotation correctness separately from performance.

The MOSAICO QSPI benchmark can select the four TE combinations and the
four NONE tile and memory combinations in one firmware. The board
configuration uses four data lines at 40 MHz, RGB565 byte swap and TE
GPIO 43. The report prints TE admission wait and transfer time separately.
The firmware built successfully. The board's USB CDC port stopped
accepting writes during the original-image backup, before test flashing.
The historical QSPI 2×2 matrix passed on this board, with up to 23.855 ms
per full-frame transfer in the separate dynamic TE test.
[Historical QSPI results](test_apps/boards/RESULTS.md)

```sh
dev/tools/run.py build --board s31-qspi --angles 0 --profile \
  --profiles TE_C1_T1,TE_C1_T2,TE_C2_T1,TE_C2_T2,GRAM_T1_INTERNAL,GRAM_T2_INTERNAL,GRAM_T1_PSRAM,GRAM_T2_PSRAM \
  --lvgl-dir /path/to/lvgl-9.5.0
```

P4 DPI and BOX-3 SPI built in the all-mode review. In that baseline,
the DPI driver writes back a registered scanout FB inside
`dpi_panel_draw_bitmap()`; Present also writes back the same full range
in `scanout_submit()` for DPI. The P4 follow-up below covers the fix.
I80 uses the Panel IO path and has no current hardware result.

Host passed 52/52 with LVGL 9.5.0, 52/52 under ASan/UBSan and 51/51
with LVGL 9.2.2. Both RGB boards were restored and each full 16 MiB
flash matched its original backup digest.

### Korvo internal T2 reopen

The baseline ran `DOUBLE_PARTIAL/T2`, closed it, and opened
`TRIPLE_PARTIAL/T2` without resetting the board. Instrumentation captured
the failure in the second call to `heap_caps_aligned_alloc()` for the FB
tile pool. It requested 80,000 bytes with 64-byte alignment and
`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`. The first tile succeeded.
Afterward the DMA heap had 258,751 free bytes and the largest allocatable
block was 79,872 bytes.

FB PARTIAL now tries one aligned internal DMA allocation for both tiles
when the heap can fit the combined size. Each tile keeps its own base,
capacity and lease. If the combined allocation is unavailable, the
backend tries two separate allocations. An explicit T2 that cannot
allocate both tiles returns `ESP_ERR_NO_MEM`. The Host regression exercises
close and reopen, real frame submission with each lease, the separate
allocation path, and the existing explicit-count OOM case.

The sequential retest used the same 800×480, RGB565, 0°, XIP-on
configuration with LVGL 9.5.0 and 16 scenes per mode. The first open
used the combined allocation. On reopen the largest internal DMA block
was 159,744 bytes, so the backend allocated two separate tiles. The
benchmark queried both pools and verified two distinct internal
DMA-capable addresses with 50 lines and 80,000 bytes per tile.

| Mode, in run order | Baseline commit FPS / P99 | Fixed commit FPS / P99 |
|---|---:|---:|
| DOUBLE_PARTIAL/T2 | 28.6 / 77 ms | 28.3 / 77 ms |
| TRIPLE_PARTIAL/T2 | Reopen failed | 31.2 / 77 ms |

Both fixed cases passed the buffer audit and closed with zero buffer-wait
timeouts and zero supply misses. Host passed 52/52 with LVGL 9.5.0,
52/52 under ASan/UBSan and 51/51 with LVGL 9.2.2. The resource test
also passed with LeakSanitizer enabled. The Korvo's original 16 MiB flash
matched its backup digest after restoration.

### P4 DPI framebuffer writeback

On ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`, the native DPI
`dpi_panel_draw_bitmap_2d()` writes the clipped rows of a registered FB
with C2M and UNALIGNED before updating `cur_fb_index`. Its external
source branches use the configured copy hook or a CPU copy with a
destination writeback. Present delegates only for the audited P4 path
with matching physical dimensions and input pixel size, and submits an
exact registered FB base, tight stride and full physical rectangle.
The existing ISR retirement fence stays in the same submit path.

The profiled P4 EK79007 benchmark built with LVGL 9.5.0, XIP on and all
six RGB/DPI mode selections. The built adapter enabled
`PRESENT_IDF_DPI_FRAME_WRITEBACK` and linked the EK79007 BSP facts.
The P4 pixel matrix and S31 RGB regression firmware also built. Host
and ASan/UBSan each passed 52/52. The audited and conservative Host
variants verify the writeback counts for RGB565 and RGB888, mismatch and
missing BSP facts, rejected partial or non-base submissions, and the
retirement fence with early callbacks. The P4 was absent from the
Windows and WSL inventories during this series.

#### EK79007 board A/B

The board matched the Function EV rev3 entry by chip revision v3.2 and
MAC `e8:f6:0a:e7:a7:8d`. The EK79007 2.0.2 driver initialized the
1024×600 RGB565 panel, with the same driver FB pool, LVGL 9.5.0, 0° and PSRAM
XIP on in both boots. The benchmark ran 16 scenes each for
`TRIPLE_FULL`, `DOUBLE_DIRECT` and `TRIPLE_PARTIAL`, representing the
three drawing contracts. PARTIAL used one 50-line tile. The build
manifests matched SDK, profile configuration, cases and production
source digests. A disables the
BSP delegation decision in the test fixture. B uses the normal verified
DPI route.

Test-only wrappers correlate the cache calls to each physical
`draw_bitmap` invocation by task and framebuffer base. Each counted
call covers all 1,228,800 bytes with C2M and UNALIGNED. The trace reports
per-submission minima and maxima separately for the call before entering
the driver and the call inside it.

| Mode | Commit FPS A → B | Begin-to-commit P99 A → B | C2M calls per submit A → B | Mean C2M µs per submit A → B |
|---|---:|---:|---:|---:|
| TRIPLE_FULL | 27.8 → 27.7 | 70 → 70 ms | 2 → 1 | 420.5 → 413.2 |
| DOUBLE_DIRECT | 28.6 → 28.7 | 60 → 60 ms | 2 → 1 | 332.0 → 325.5 |
| TRIPLE_PARTIAL | 32.3 → 32.5 | 77 → 77 ms | 2 → 1 | 268.7 → 259.2 |

All 6,146 A submissions had one Present call followed by one driver
call. All 6,154 B submissions had one driver call. The per-submission
minima and maxima match those counts in every mode. Both groups passed
the buffer and serial audits with zero cache errors, buffer-wait timeouts
and DSI underrun messages. PARTIAL recorded 17 scanout supply misses in
each group. The observer reported no visible anomalies in either group.
The optimized pixel matrix also passed all 24 combinations of six
mode selections and four initial angles, comparing all pixels in 96
submitted frames against the reference. The full original 16 MiB flash
matched its backup digest after restoration.

### P4 TRIPLE_FULL render bottleneck

The previous DPI writeback run measured 31.76 ms of render interval and
0.42 ms inside panel submission per frame at 27.7 commit FPS. Buffer
waits and Present executor copies were zero. A separate sampling build
measured 54.90 seconds in large opaque whole-row RGB565 fills, 26.78
seconds in other RGB565 fills, 9.96 seconds in RGB565 image blends and
0.99 seconds in the measured ARGB8888 functions. These times sum blend
calls from both software workers and can overlap. The large fills were
59% of the sampled blend interval.

P4 now uses the existing lease-checked PPA fill path for native
1024×600 RGB565 triple FULL with the audited two-worker LVGL 9.5
configuration. SW and PPA builds used the same EK79007, firmware
configuration, scene workload and DPI writeback path. The control
disabled the optional PPA client through the benchmark wrapper.

| Metric | SW fill | PPA fill |
|---|---:|---:|
| Commit FPS | 27.8 | 40.0 |
| Begin-to-commit P99 | 70 ms | 55 ms |
| Mean render interval | 31.75 ms | 17.66 ms |
| Mean panel submission interval | 0.424 ms | 0.345 ms |
| Buffer wait total | 0 s | 8.22 s |
| Buffer-wait timeouts / supply misses | 0 / 0 | 0 / 0 |
| PPA fill requests / errors | 0 / 0 | 5,863 / 0 |

Two full-frame RGB565 CRCs matched (`5ae62fc7` and `0bd18cb5`).
The 16-scene runs passed buffer and serial audits and logged zero DPI
underruns. The original 16 MiB flash matched its backup after the A/B.

## Three-board Present and adapter comparison (2026-11-15)

S3 Function EV RGB, S31 Korvo RGB and P4 Function EV with EK79007 ran
the same RGB565 benchmark on ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`
and LVGL 9.5.0 with PSRAM XIP enabled. Each board passed 24 Present cases
and 20 adapter cases at 0°, 90°, 180° and 270°. The adapter marked its
four AUTO cases as unsupported. All 60 paired cases matched the audited
LVGL input buffer sizes and memory domains. P4 adapter stopped logging
at `DOUBLE_DIRECT/270°` in the first run. A fresh boot under the same
configuration completed that case and the remaining eight. All three
original flash images were restored and verified.

Each row covers 20 paired mode-angle combinations. SRAM lists ordinary
static ELF sections and peak internal heap use separately. The heap
columns use the highest observed value for each implementation.

| Board | Mean LVGL FPS Present / adapter | Static SRAM (KiB) | Peak internal heap (KiB) | Peak PSRAM heap (KiB) |
|---|---:|---:|---:|---:|
| S3 Function EV | 30.15 / 29.50 | 80.42 / 78.88 | 242.29 / 249.04 | 2388.20 / 1576.13 |
| S31 Korvo | 30.90 / 32.40 | 91.12 / 90.87 | 266.93 / 275.97 | 4012.02 / 2689.08 |
| P4 Function EV | 29.20 / 35.15 | 97.25 / 97.12 | 286.83 / 296.12 | 6296.41 / 4058.08 |

P4 native `TRIPLE_FULL/0°` scored 47 / 33 LVGL FPS. Present also
measured 39.8 commit FPS and 55 ms begin-to-commit P99 in this matrix.
Rotated DIRECT uses two extra full-size surfaces in Present, one for
LVGL and one for the backend. The adapter uses a panel-pool framebuffer
for LVGL at these angles. On P4, `DOUBLE_DIRECT/90°` used 2416.04 KiB
of additional PSRAM at open for Present and 3.22 KiB for adapter.

## P4 DMA2D and PPA SDK compatibility A/B (2026-11-15)

The ESP32-P4 Function EV board used an EK79007 1024×600 display, RGB565 over
MIPI DPI, ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`, LVGL 9.5.0 and
PSRAM XIP enabled. Both builds used one input tile with a 50-line budget,
identical benchmark instrumentation, and the same 16 LVGL scenes per case.
A used the executor selection from the preceding three-board matrix, where
Present's copies ran on CPU. B used the available SDK DMA2D and PPA APIs and
reused the FB backend worker for queued copies. The separate LVGL fill policy
was the same in A and B.

Commit FPS is committed frames divided by benchmark duration. LVGL FPS is
the mean of the scene FPS values reported by LVGL. Full-frame P99 measures
from the attempt to acquire the next frame through the driver's scanout
confirmation. It includes buffer waiting, rasterization, copy and scanout
waiting. The benchmark uses 1 ms histogram bins and excludes the initial
frame if it began before the measurement reset. Both builds include the
same 3,432 B of benchmark-only timing state.

Memory is measured per case from open through the end of the scene run.
SRAM includes the ordinary static IRAM/DRAM sections and peak internal
8-bit heap use. PSRAM includes peak external heap use plus bytes outside
the heap, including XIP reservations. The SDK sums low-water marks across
heap regions, so these peaks are conservative totals.

The Host and ASan/UBSan suites each passed 57 tests. They cover both DMA2D
APIs, cache and row alignment, RGB565/RGB888, PSRAM, client isolation, and
in-flight copy ownership when callbacks are absent or late. P4 benchmark
and pixel firmware compiled against the SDK above. S3 and S31 smoke
firmware also compiled with that SDK.

| Mode / angle | Commit FPS A → B | LVGL FPS A → B | Full-frame P99 (ms) A → B | SRAM (KiB) A → B | PSRAM (KiB) A → B | Hardware copies in B |
|---|---:|---:|---:|---:|---:|---:|
| `DOUBLE_FULL` / 0° | 21.7 → 21.6 | 25 → 25 | 99 → 99 | 315.03 → 314.86 | 4929.16 → 4954.47 | 0 |
| `DOUBLE_FULL` / 90° | 10.5 → 15.8 | 13 → 20 | 169 → 104 | 317.50 → 315.36 | 6048.16 → 6134.45 | PPA 1,099 |
| `TRIPLE_FULL` / 0° | 39.8 → 39.6 | 47 → 47 | 68 → 68 | 313.19 → 312.01 | 4896.29 → 4874.59 | 0 |
| `TRIPLE_FULL` / 90° | 11.7 → 18.3 | 15 → 23 | 158 → 106 | 319.02 → 314.87 | 6208.18 → 6181.45 | PPA 1,274 |
| `DOUBLE_DIRECT` / 0° | 28.7 → 28.6 | 35 → 35 | 82 → 82 | 319.38 → 314.20 | 4767.15 → 4732.84 | 0 |
| `DOUBLE_DIRECT` / 90° | 17.3 → 20.2 | 25 → 29 | 115 → 81 | 317.12 → 311.84 | 7183.16 → 7170.43 | PPA 10,309 |
| `DOUBLE_PARTIAL` / 0° | 29.1 → 35.6 | 40 → 48 | 94 → 79 | 386.65 → 384.20 | 4725.14 → 4737.84 | DMA2D 25,642 |
| `DOUBLE_PARTIAL` / 90° | 22.6 → 29.7 | 33 → 43 | 130 → 103 | 363.46 → 359.83 | 4647.14 → 4687.42 | PPA 30,800 |
| `TRIPLE_PARTIAL` / 0° | 32.7 → 39.1 | 45 → 53 | 89 → 76 | 386.62 → 383.41 | 4725.14 → 4737.84 | DMA2D 30,533 |
| `TRIPLE_PARTIAL` / 90° | 26.9 → 34.6 | 39 → 50 | 119 → 93 | 363.46 → 359.83 | 4647.14 → 4687.42 | PPA 38,476 |

Both groups passed ten cases and 160 scenes. Their measured input buffer
layouts match, and both use 103,024 B of static SRAM. The highest case SRAM
peak dropped from 386.65 to 384.20 KiB. PSRAM available to the heap dropped
by 12,672 B after mapping the larger B image with XIP. PSRAM totals above
include that reservation. Per-case totals also reflect each scene run's
allocation high-water marks.

B ran 56,175 DMA2D copies and 81,958 PPA copies. The executor probe routed
543 copies to CPU before hardware submission. Both groups reported zero
buffer wait timeouts and zero incomplete timing records after drain. The P4
executor suite matched the CPU reference in all 32 cases, including 20
hardware cases and two-client destruction checks for both engines. The
separate P4 pixel matrix passed all 24 mode/angle cases with full-frame and
dirty-region pixel checks. The pixel checks verify buffer contents against
the reference. No optical observation was collected for this run. The
original 16 MiB flash image was restored and verified against its backup.

## Archived five-board coverage (2026-11-11)

Each passing case completed 16 LVGL scenes and closed successfully. Angles:
0/90/180/270 degrees; creation rotation is distinct from runtime rotation.

| Board | Performance present / adapter | Address diagnostics present / adapter |
|---|---:|---:|
| S3 RGB 480x480 | 24 / 20 | 24 / 20 |
| S31 Korvo RGB 800x480 | 24 / 20 | 24 / 20 |
| P4 DSI 1024x600 | 24 / 20 | 24 / 20 |
| S31 MOSAICO QSPI TE 480x480 | 16 / 9 | Not collected |
| BOX-3 SPI 320x240 | 20 / 4 | Not collected |

Total: 181 performance cases, 132 diagnostics, 5008 scenes. Scanout uses XIP ON
and excludes NONE; S3 has 10-line bounce, S31 RGB has no bounce. TE native
configurations are listed separately; SPI GRAM permits tearing.

## Findings that remain relevant

- Address hits (release / write / confirmation): present 0/0/0 on each scanout
  board; adapter S3 0/2/2 across 2 cases, S31 0/0/0, P4 1/4/1 across 5 cases.
  Categories can overlap. Independent input buffers outside the scanout pool
  are not automatically errors; uninstrumented paths are not counted as zero.
- S3 matched 20 cases: present 163 short-supply frames / 83830 measured scans;
  adapter 96 / 83827. Existing scan/DMA restarts were respectively 163 and 96,
  not MCU resets. Zero address hits do not resolve this supply/visual risk.
- Memory reports four remaining-heap points (before/open/minimum/close) and
  actual ELF static SRAM separately. Whole-image costs are not component-only
  costs. Remaining heap is better when higher; static use is better when lower.
- Old PPA stalls and USB interruptions remain historical evidence, not current
  passing cases. Completed diagnostics do not replace optical validation.

## Download and reproduce

[Full five-board report, JSON/CSV, diagnostic details and history index](https://gitlab.espressif.cn:6688/-/project/274/uploads/3d4d3f108b6592ebdddc269ffa0ab5b4/present-five-board-results-20261111.tar.gz)
(352656 bytes; SHA-256 `27af070a4fadfd2a1ef90adb1c4456b6966485b43d951edf79fb18682796235f`).

Download [the matching report tools](https://gitlab.espressif.cn:6688/-/project/274/uploads/76639e147d43ba6eaa4aa187f650154a/present-report-tools-and-legacy-probe.tar.gz) and extract them to
`/path/to/tools`. SHA-256: `a918656f17e01d076c424c8802cf0560ba43bac21057cd27a13e5cc76c95dde4`.
The bundle also retains the old hardware overlay generator as historical source;
its old anchors/configuration are not claimed to work against current code.

```sh
python3 /path/to/tools/report-tools/build_latest_dashboard.py --snapshot /path/to/results/report/matrix-data.json --output /tmp/display-latest/matrix-data.json
python3 /path/to/tools/report-tools/render_comparison.py --data /tmp/display-latest/matrix-data.json --output /tmp/display-latest/comparison.html
```

The export requires the downloaded snapshot explicitly; it does not rely on an
in-repository historical dataset. Host tests need no report attachment.
See [development commands](README.md) and [raw evidence boundaries](ARCHIVE.md).
