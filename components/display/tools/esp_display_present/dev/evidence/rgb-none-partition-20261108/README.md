# S3 RGB NONE partition path

Date: 2026-11-08

## Change

Public RGB `NONE` now uses the partition/GRAM producer contract. LVGL renders
bounded tiles and `esp_lcd_panel_draw_bitmap()` copies each dirty region into
the RGB driver's single framebuffer. Present no longer borrows that framebuffer
as a persistent DIRECT canvas and no longer waits for framebuffer exposure to
release a tile.

The RGB partition adapter releases a source on the driver's
`on_color_trans_done` callback. Callback removal is followed by an ISR barrier
before adapter storage can be destroyed. Other framebuffer modes and MIPI DSI
`NONE` keep their existing scanout path.

## Hardware result

- Board: ESP32-S3 480x480 function board / GC9503, 8 MiB PSRAM
- MAC: `7c:df:a1:fd:c6:04`
- IDF: `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`
- LVGL: `9.6.0~1`
- Workload: official LVGL benchmark, 16 valid scenes, rotation 0

| Implementation | LVGL FPS | render | flush |
|---|---:|---:|---:|
| present, old RGB NONE DIRECT path | 40 | 17 ms | 11 ms |
| present, RGB NONE PARTITION path | 50 | 15 ms | 4 ms |
| esp_lvgl_adapter NONE/PARTIAL | 51 | 16 ms | 3 ms |

The corresponding whole-system heap snapshot is:

| Implementation | Internal used after open | PSRAM used after open | Internal open delta | PSRAM open delta | Internal minimum free | PSRAM minimum free |
|---|---:|---:|---:|---:|---:|---:|
| present, RGB NONE PARTITION | 166.21 KiB | 1401.21 KiB | 75.68 KiB | 47.03 KiB | 129.63 KiB | 5834.88 KiB |
| esp_lvgl_adapter NONE/PARTIAL | 188.55 KiB | 1354.14 KiB | 54.70 KiB | 0.03 KiB | 128.85 KiB | 5874.71 KiB |

Used heap is `total-free` after display open and includes firmware and benchmark
resident allocations; it is not the component's isolated footprint. Open delta
is the change from `before_open` to `after_open`.

The new present run completed with `2428` committed frames and `SUITE_PASS`.
The one-FPS difference from adapter is within this single-run benchmark's
integer resolution. NONE permits tearing in both implementations.

Existing S3 four-angle results remain applicable to DOUBLE/TRIPLE FULL,
DOUBLE DIRECT, DOUBLE/TRIPLE PARTIAL and AUTO because those paths are unchanged.
Adapter NONE has no software-rotation equivalent; the old present NONE results
at 90/180/270 degrees describe the removed DIRECT path and must not be reused as
new partition-path measurements.

## Verification

- Host: 42/42 tests passed.
- ESP32-S3 benchmark firmware built successfully with the pinned IDF.
- Final application image was written to the board and verified by esptool.
- The board test mapping selects R8/R16 pin variants from detected PSRAM size.
