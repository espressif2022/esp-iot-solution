# V2 board acceptance application

This independent ESP-IDF application exercises **Presenter → FB → LCD scanout**
using synchronous CPU rendering and driver-owned PSRAM framebuffers. It does not
link V1, LVGL, Wi-Fi, audio, storage or touch services. It is not a power benchmark.

## Confirmed hardware

| Target | Board / panel | Identity | Console |
|---|---|---|---|
| S3 | LCD EV Board, R16 module, GC9503 480×480 RGB565 | `90:e5:b1:d0:a9:d8`, revision 0.2, Flash/PSRAM 16 MB | `/dev/ttyUSB0` |
| P4 | Function EV Board, EK79007 1024×600 MIPI-DSI RGB565 | `e8:f6:0a:e7:a7:8d`, revision 3.2, Flash 16 MB | `/dev/ttyACM0` |

Port names can change. Verify chip and MAC before flashing. S3 R16 uses I2C
SDA47/SCL48 and RGB D6=8/D7=18; R8 pins are different. P4 is built for revision
3.1+, not pre-v3 silicon. Pin/timing references are Espressif BSP S3 **4.1.0** and
P4 **noglib 5.2.3**. Only the panel/expander drivers are linked, not those BSPs.

## Build offline

Use the audited local ESP-IDF 6.2 tree. `prepare_deps.py` copies pinned official
drivers from Component Manager's local cache, retaining licenses and sources.
It replaces dependency CMake files with explicit IDF requirements and version
macros; no dependency downloads are performed. `deps/manifest.json` records
original source hashes. Pass `--cache PATH` for another cache location.

```sh
source /home/xuxin/esp_work/esp-idf-qspi/export.sh
python3 prepare_deps.py
idf.py -B build-s3 -DIDF_TARGET=esp32s3 -DSDKCONFIG=sdkconfig.s3 build
idf.py -B build-p4 -DIDF_TARGET=esp32p4 -DSDKCONFIG=sdkconfig.p4 build
```

After confirming identity and backing up the original flash:

```sh
idf.py -B build-s3 -DSDKCONFIG=sdkconfig.s3 -p /dev/ttyUSB0 flash monitor
idf.py -B build-p4 -DSDKCONFIG=sdkconfig.p4 -p /dev/ttyACM0 flash monitor
```

The build writes bootloader, partition table and test application. Full original
16 MB flash backups are kept locally under ignored `artifacts/`; restoring the
matching full backup restores the original flash layout/content. Never restore
one board's backup to the other board. The bootloader/partition table are part
of this test firmware, so application-only updates are insufficient initially.

## First acceptance stage

- Two physical framebuffers; FB0 is declared initially scanning.
- S3 continuous RGB uses two internal bounce buffers of 10 scanlines.
- P4 uses two DSI lanes at 1000 Mbps and the driver's 60 Hz EK79007 timing.
- Native RGB565; zero rotation; no DMA2D/PPA, async copy or background worker.
- Each frame follows begin/acquire/draw/submit/commit/wait/release/service.
- Wait requires `PRESENT_CONFIRMED`, terminal status and `ESP_OK` within 2 s.
  The V2 adapter fences submissions with two subsequent frame-complete events.
- Red, green, blue, white, black, yellow, cyan and magenta appear for ~1 s each;
  then eight vertical color bars, a white border, a moving white bar and a binary
  frame counter. The sequence repeats every 240 frames.
- Logs report confirmed frames, wait time, pending receipts, sticky errors and
  internal free heap. A failure retains resources and prints the failing stage
  every 5 s; it does not free memory that DMA might still access or reboot-loop.

This first runner intentionally keeps the panel and board buses alive until
reset. It does **not yet test stop/recreate, touch, LVGL, DIRECT/PARTIAL, rotation,
RGB888 or acceleration**. Those are separate acceptance stages after this
baseline. Receipt success proves callback progress, not correct visible pixels
or tear-free timing. Confirm color order, border, moving bar and corruption on
the physical screen separately.

## Current runner: five-mode lifecycle suite

The current `main/main.c` supersedes the initial FULL-only pattern loop above.
It cycles through DOUBLE_FULL, TRIPLE_FULL, DOUBLE_DIRECT, DOUBLE_PARTIAL and
TRIPLE_PARTIAL, 48 frames per case, then repeats. One through five white squares
at the top identify the mode. A 37×59 white box moves over fixed color bars.

The first frame covers the whole screen. Subsequent DIRECT/PARTIAL frames only
redraw the previous and current box rectangles. PARTIAL uses 11-row scratch
bands; dirty regions have a three-pixel padding in their source stride. The
whole submitted framebuffer is compared against the expected image after each
PRESENT_CONFIRMED, covering preservation outside damage as well as band copies.
This is CPU memory verification, supplemented by physical visual inspection;
it does not independently measure DMA bus output or optical tearing.

Every case drains receipts, stops/destroys Presenter and scanout, deletes the
board's IO/bus/expander/LDO resources, and checks heap integrity. S3's 3-wire IO
is already deleted by the GC9503 constructor. Each subsequent case creates a
new driver pool and restores the FB0 initial-scanout reservation. Logs include
`PASS`, `CLEAN`, and `SUITE_PASS`. A brief blank at mode changes is expected
because the actual panel is deleted and reinitialized.

The producer waits for each frame before the next one; therefore this suite
validates three-buffer configuration/allocation and lifetime, **not a saturated
three-frame pipeline, guaranteed use of the third buffer, or backpressure stress**. Rotation, RGB888, LVGL,
acceleration and touch remain separate stages.

Capture and summarize at least two complete cycles (no automatic reset):

```sh
python capture.py /dev/ttyUSB0 artifacts/s3-suite2.log --seconds 180
python analyze_suite.py artifacts/s3-suite2.log --min-cycles 2
```

Run a separate capture for P4. The analyzer rejects failure/pixel-corruption
logs, repeated startup, incomplete mode sets, duplicate completed cases and
varying per-mode post-destroy heap across completed cycles. A trailing partial
cycle is excluded from totals. It does not replace visual confirmation.
