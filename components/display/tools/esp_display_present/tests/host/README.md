# Component regression tests

This directory contains Host regressions of production code. The target smoke
application lives in `../../test_apps/smoke`; `../run.py` is the shared entry.
Benchmark datasets and board-specific diagnostics are external to the repository
and are not required by formal tests or distributed packages.

## Host

CMake, a C/C++ compiler, Threads and Python 3 are required. From the component root:

```sh
python3 tests/run.py test
python3 tests/run.py test --sanitize
python3 tests/run.py test --lvgl-dir /path/to/lvgl
python3 tests/run.py test --lvgl-dir /path/to/lvgl --sanitize
```

LVGL tests require an existing LVGL 9 source checkout; this runner never downloads
it. Output defaults to `/tmp/esp-display-present-tests`; `--build-dir` overrides it.
Tests cover buffer ownership, late callbacks, rollback, timeouts, rotation,
planning, hardware API variants and the LVGL binding. Use the actual CTest count
for the selected configuration. Host tests do not prove optical tear freedom.

The handoff regressions compile the real recovery helper and caller from
`examples/display/gui/gsp_lvgl_present_handoff`, using only platform mocks.
Keep that example available when exporting the Host test tree. Its source is
also covered by the Host CI change rules.

An optional integration test accepts `--gsp-dir /path/to/gsp` together with
`--lvgl-dir`. It supports both the split `components/esp_gsp_adapter` layout and the
monolithic `src/backend/esp_lcd` / `src/platform/esp_idf` layout. Both require
`cmake/gsp_portable_targets.cmake` and the generated Host fixture
`ci/host/data/out_nav/nav.gspb`. The test compiles the checkout's actual display,
canvas and runtime sources. It checks GSP → LVGL → GSP pixel restoration and
retrying a failed resume after acquiring a live Presenter lease. Host mocks
cover task, panel, touch and accelerator boundaries; they do not validate
hardware timing, touch input or hardware acceleration. No GSP source or bundle
is vendored.

## ESP-IDF smoke

Activate ESP-IDF 6.0 or newer, then run:

```sh
python3 tests/run.py idf-build --target esp32s3
python3 tests/run.py idf-build --target esp32p4
```

This application verifies target API and link integration using a loopback
fixture. It is not a panel benchmark and does not access a real display.
Host scanout tests additionally exercise both modern DPI callbacks and ESP-IDF 6.0
`on_refresh_done` retirement semantics (`rev < 3.0` DMA-EOF and `rev >= 3.0`
Bridge-VSYNC).

## Dependency scheduling regression

`dependency` exercises bounded writes to a retained canvas while copy descriptors
are queued or running. Disjoint writes may proceed; overlapping and full-canvas
leases remain pinned until real copy completion. The descriptor pool is bounded
by `PRESENT_MAX_DAMAGE_AREAS`; queue exhaustion is retryable backpressure.

The worker distinguishes construction from scanout/deadline work, independently
of rotation and scene content. Tests assert post-unlock notifications,
no worker gate acquisition for construction-only copies, prior scanout
retirement, FIFO pixel results, cancellation, and late completion after bounded
STOP. Priority is fixed within a producer session: tests use priorities 3/6/9,
check creator and cross-thread producer binding, failed binding, explicit fixed
overrides, and no changes on begin/submit/commit. `resource_priority_override`
repeats the policy matrix with a nonzero component override for FB/TE/REGION.
Host priority hooks validate selection and ordering, not FreeRTOS timing or FPS.

### Panel IO NONE convergence

`panel_io_emit` exercises the production `lcd_io.emit` entry with 288 independent
wire-image comparisons: RGB565/native and swap, RGB888, all four rotations,
packed/padded source rows, offset rectangles, tails, and early/delayed callbacks.
It bounds staging to two allocations of at most 16 KiB and checks source bytes.
Additional cases cover direct swap, PSRAM address/length fallback, allocation and
cache rejection/retry, and real engine WORKER output from dual 480x480 PSRAM D
at all rotations, while the producer draws D1 during D0 transmission.
The existing LCD IO tests cover staged abort/timeout/late-callback retirement;
REGION tests cover lease restoration, FIFO retry, faults, cancellation and stop.
These are host lifecycle/pixel checks; hardware DMA throughput remains unmeasured.
