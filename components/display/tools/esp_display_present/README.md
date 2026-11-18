# ESP Display Present

[中文](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README_CN.md)

`esp_display_present` manages frame submission, buffer ownership and display
synchronization for ESP LCD panels. It provides one renderer-facing API for
RGB/MIPI scanout and panel-IO transfers. Applications own panel and bus setup;
Present owns the lifetime of the pixel work submitted through it.

Use [esp_lv_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md) to connect LVGL 9. Custom
renderers use the public API in [include/esp_display_present.h](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h).

## Requirements

- ESP-IDF 6.0 or newer; RGB565 or RGB888 input matching the panel configuration.
- An initialized panel. RGB/MIPI require at least two driver framebuffers and a
  supported buffer-retirement protocol. DSI also requires DPI frame-complete
  and VSYNC callbacks, which ESP-IDF 6.0 does not provide.
- Panel IO requires the panel and IO handles. TE additionally needs a wired,
  enabled TE signal and enough bus time for the composed frame. For PSRAM compose
  images without direct DMA, Present sends bands through two bounded internal DMA
  staging buffers. Internal DMA images and confirmed direct-PSRAM-DMA images use
  direct submission. When driver facts cannot be queried, `hw.spi_psram_dma_direct`
  must match the actual IO creation flag.

For ESP32-S3 RGB, enable bounce buffers in the panel driver. RGB uses continuous
refresh and untransformed driver coordinates; apply logical rotation through
Present. The SDK/BSP must provide the actual framebuffer facts. With older
drivers lacking these facts, the [compatibility option in Kconfig](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/Kconfig)
may confirm an **actual bounce-buffer** setup; it does not enable non-bounce
retirement on an unsupported chip.

This checkout declares Present 2.0.0 and LVGL binding 0.2.0. To use the local
source, add the following to your application's `main/idf_component.yml` and
replace the path. The version declaration alone does not establish Registry
availability; LVGL users should select both local components as shown in the
[binding guide](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md).

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

## Create a presenter

Initialize the panel with the BSP or `esp_lcd` first. This example borrows an
RGB panel and resolves AUTO against its actual driver framebuffer pool:

```c
#include "esp_display_present.h"

// Call after the BSP has initialized a supported 480x480 RGB panel
// with at least two driver-owned framebuffers.
esp_err_t create_presenter(esp_lcd_panel_handle_t panel,
                           esp_display_presenter_t **out_presenter)
{
    const esp_display_presenter_panel_config_t lcd = {
        .width = 480,
        .height = 480,
        .hw = {
            .panel = panel,
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        },
    };
    return esp_display_presenter_open(&lcd, ESP_DISPLAY_PRESENT_MODE_AUTO,
                                      out_presenter);
}
```

For MIPI use `ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI`; for panel IO use
`ESP_DISPLAY_PRESENT_PANEL_IO` and set `hw.io`. For TE also set
`hw.te_enabled` and `hw.te_sync` to match the wiring and bus configuration.
Keep the panel, IO and their storage valid until presenter deletion succeeds;
let Present manage the completion callbacks it registers.

`open()` takes **physical** panel dimensions; `hw.rotation` determines the
logical orientation. Use `open_ex()` to select tile rows, one/two input tiles,
PSRAM preference or one/two TE compose buffers. Use `create()` for explicit
framebuffers, damage limits or timeout overrides. Its `width` and `height` are
**logical** dimensions. Query `get_caps()` and `get_effective_config()` for the
resulting layout, mode and buffer pools.

Before creating an RGB/MIPI panel, `esp_display_present_get_required_frame_buffer_count()`
can recommend its framebuffer count. This query does not allocate buffers.

## Choose a mode and pipeline combination

This table covers **one Presenter instance** created by `open()` / `open_ex()` / `create()`. An input tile is a buffer for rendering part of a frame. A full-screen draw buffer holds renderer output; a full-frame compose image holds composed TE output.

**Driver framebuffers remain driver-owned. Present pixel buffers are separate allocations; account for both columns.** Background task counts exclude application/LVGL and driver tasks, ISRs and timer callbacks. In code, `backend` advances display progress and `pixel` processes pixels; the table states how many tasks perform these roles.

RGB/MIPI use native framebuffer pixel layouts and need no software RGB565 byte swap; their paths below differ by rotation. Panel IO wire byte order follows the panel requirements. Byte swapping is separate from RGB/BGR component order.

| Panel / mode | Rotation | Driver framebuffers | Pixel buffers allocated by Present | Present background tasks |
|---|---|---|---|---|
| RGB/MIPI `DOUBLE_FULL` / `TRIPLE_FULL` | 0° | 2 or 3 frames | No additional pixel storage; draw directly into driver framebuffers | 1 display-progress task |
| RGB/MIPI `DOUBLE_FULL` / `TRIPLE_FULL` | 90° / 180° / 270° | 2 or 3 frames, retained | Add 1 full-screen draw buffer, shared by these driver framebuffers | 1 task advances display and processes pixels |
| RGB/MIPI `DOUBLE_DIRECT` | 0° | 2 frames | No additional pixel storage; draw dirty regions directly into driver framebuffers | 1 task advances display and repairs content |
| RGB/MIPI `DOUBLE_DIRECT` | 90° / 180° / 270° | 2 frames, retained | Add 1 full-screen draw buffer, shared by both driver framebuffers | 1 task advances display and processes pixels |
| RGB/MIPI `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | 0° / 90° / 180° / 270° | 2 or 3 frames | 1 input tile; copy/rotate directly into driver framebuffers, without another full frame | 1 task advances display and processes pixels |
| RGB/MIPI `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | 0° / 90° / 180° / 270° | 2 or 3 frames | 2 input tiles; copy/rotate directly into driver framebuffers, without another full frame | 2 tasks: 1 for display progress, 1 for pixel processing |
| IO `NONE`, `CALLER` / `DEFAULT` | 0° / 90° / 180° / 270° | None | T=1/2 internal or PSRAM D; lazily add 2 bounded internal S slots for conversion or non-DMA source¹ | 0; caller processes pixels and submits |
| IO `NONE`, `BACKGROUND` (WORKER) | 0° / 90° / 180° / 270° | None | T=1/2 internal or PSRAM D; same bounded S, no input-sized X | 1 backend task for conversion, band filling and submission |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | None | 1 input tile, 1 full-frame compose image; see transfer staging¹ | 1 task: display progress and pixel processing |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | None | 2 input tiles, 1 full-frame compose image; see transfer staging¹ | 1 task: display progress and pixel processing |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | None | 1 input tile, 2 full-frame compose images; see transfer staging¹ | 2 tasks: 1 for display progress, 1 for pixel processing |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | None | 2 input tiles, 2 full-frame compose images; see transfer staging¹ | 2 tasks: 1 for display progress, 1 for pixel processing |

¹ NONE and TE use `CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES`, default 16 KiB per slot rounded down to physical whole rows, with a separate pair per instance. `sdkconfig.rename` migrates the old TE setting. NONE allocates lazily; TE allocates at creation for non-direct PSRAM compose images. Alternate filling and sending with at most one DMA band in flight. Tails send real pixels without wire padding. Reject a physical row exceeding the budget. Stacks, metadata and driver buffers are additional.

### Automatic selection

`AUTO` resolves to a concrete mode before following the table: RGB/MIPI selects `TRIPLE_PARTIAL` with three driver FBs or `DOUBLE_PARTIAL` with two. Panel IO selects `TE_SYNC` with a valid TE configuration, otherwise `NONE`.

RGB/MIPI do not support `NONE` or `TE_SYNC`. A zero-initialized `mode` selects `NONE`; request `AUTO` explicitly. TE effectiveness depends on panel timing and transfer budget; it cannot eliminate tearing in every configuration.

### Buffer configuration

Use the `drawbuf` argument of `open_ex()`, or `target.drawbuf` in `create()`:

- `lines`: rows per input tile. Zero uses `CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES` (default 32 KiB), rounded to whole logical rows, with at least one row and at most the logical height. Explicit rows override this default; the budget is not a hard limit on a single row.
- `buffers`: input tile count T, either 1 or 2. Zero selects automatically; only an automatically selected pair can retry allocation with one tile. FULL/DIRECT do not use this pool.
- `in_psram`: prefer PSRAM for input tiles; allocation may fall back to internal DMA memory.
- `te_compose_buffers`: TE compose-image count C. Values 0/1 select one image; 2 selects two. Applies only to `TE_SYNC`, independently of tile count.

### Asynchronous processing and rotation

IO `NONE` selects only the execution task. `target.io_submit = CALLER` or `DEFAULT` (zero) processes pixels, fills bands and submits on the caller; `BACKGROUND` uses one backend output task with the backend priority policy below. Memory location, swap and rotation never add tasks. Set this field through `create()`; `open()` / `open_ex()` use `DEFAULT`. Other modes still require `DEFAULT`.

Recommended profiles are CALLER + one internal D, BACKGROUND + two internal D, and BACKGROUND + two PSRAM D. Both counts and memory domains use the same implementation. NONE retains PARTITION semantics; D may cover the whole screen without introducing FB FULL persistent-canvas semantics.

Direct PSRAM DMA requires matching SoC/IO support and legal actual source address and length. A direct swap uses in-place RGB565 swap16. Rotation generates physical rows in S; alignment failures fall back to S without expanding damage or padding wire pixels to 64 bytes. Source-stride handling is a read detail, not another execution mode.

Runtime `set_rotation()` preserves task count and allocates no input-sized X. An instance using in-place swap may switch to rotated staged output. Quarter-turns exchange logical width and height. Drain first, query the new layout, and handle geometry errors such as a tile too small for one row.

With one TE compose image, the display-progress task also runs the asynchronous pixel queue; no separate pixel task is created. Two compose images retain a dedicated pixel task to overlap composition and transmission. Sharing the task preserves asynchronous requests, completion callbacks and buffer ownership.

Two input tiles can alternate, allowing application rendering to overlap background pixel processing. Two TE compose images allow composition of the next frame while the previous frame transfers; one image must wait for transfer retirement before being overwritten. More buffers do not automatically add background tasks or guarantee higher frame rates.

Asynchronous pixel processing can advance in the background using PPA/DMA2D or the CPU. Pixel requests are still processed serially. Whether bus transfers are asynchronous is determined by the output path.

All backend tasks use `CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY`
(`Display presenter` in menuconfig). The default, 0, snapshots the creating
producer task's priority, and selects the new producer's current priority on a
successful rebind or LVGL session bind. This avoids inheriting the setup task's
priority for a later LVGL producer. A nonzero value fixes the application-selected
priority across sessions. Submit, commit, completions and scene changes never
retune it; changing the producer task's priority alone does not retune an active
session. Dedicated pixel workers keep their existing first-submit priority policy.

An application needing display output to precede rendering should select an
explicit priority relative to its own tasks. Neither a fixed number nor matching
the producer guarantees a scanout/TE deadline; verify scheduling, XIP, bus
bandwidth and measured transfer duration for the application.

### TE setup and limits

Enable TE output in the panel driver, connect its signal, and set `te_enabled`, `te_sync.gpio_num`, `bus_freq_hz` and `data_lines` to match the hardware. Bus settings seed the first transfer estimate; subsequent admission uses measured timing, including gaps between staged bands.

Startup polarity detection samples the GPIO for approximately 9 ms and assumes a short blanking pulse with a longer idle level. It adds no background task or periodic sampling. A different TE waveform needs separate validation. Buffer count alone does not guarantee that transmission fits the TE window; see [TE timing and admission](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md).

### Memory and effective configuration

Input tiles (T), TE compose images (C), driver FBs and additional pixel buffers consume **separate storage**. Driver and Present pixel storage are listed separately in the table. Allocation and release follow these rules:

- RGB/MIPI FULL/DIRECT: 0° borrows driver FBs directly. A rotation of 90°/180°/270° adds **one shared logical frame**, not one per driver FB. This frame prefers PSRAM and may fall back to internal DMA memory. Returning to 0° releases it.
- PARTIAL: allocate T input tiles and copy/rotate directly into the candidate driver FB. Rotation adds neither a full-frame canvas nor a separate conversion-tile pool.
- IO NONE: allocate only T draw buffers D. Directly send a tight 0° source when its actual DMA constraints pass; apply in-place RGB565 swap16 and cache writeback when needed, restoring pixels on rejection. Otherwise fuse rotation, swap and source-stride reading into physical bands in two bounded S slots. Retain D until the entire logical transfer completes; reuse each S only after its DMA completes. No input-sized X or pixel task.
- TE_SYNC: allocate T input tiles and C physical full-frame compose images. Rotation writes directly into those images without another rotation frame. Compose images prefer PSRAM and may fall back to internal DMA memory. Non-direct PSRAM images add the two staging buffers above, independently of T and C. The compose image remains pinned until logical transfer completion. Stop or timeout prevents later bands from being submitted; an in-driver band still requires its real completion callback before retirement. A timed-out stop retains the instance for retry; pixels already written to the panel cannot be rolled back.

These counts do not cover all application memory. Driver-owned RGB bounce buffers, DMA descriptors, frontend-owned draw buffers (for example LVGL buffers), task stacks, metadata and alignment cost extra. One 480×480 RGB565 image costs 450 KiB; one 480×32 RGB565 tile costs 30 KiB.

After creation, inspect `get_effective_config()` for the actual mode, `tile_count`, `image_count`, memory domains and degradation flags. Use `get_caps()` for the render contract. These APIs do not enumerate every lazily allocated buffer; measure runtime memory for the actual path. See [mode and buffer contracts](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md).

## Render and release safely

For a custom renderer, the normal sequence is `begin_next_frame()` →
`acquire_buffer()` → draw/`submit_buffer()` → `commit_frame()`. PARTITION
renderers may acquire and submit multiple regions within one frame. Follow the
returned contract and lease capacity; a submitted buffer is no longer writable.
LVGL users should let the binding perform these operations.

Keep renderer calls on the owning producer task; `open()` initially binds its
calling task. To move a custom renderer to another task, use
`rebind_producer()` between frames. If the next adapter cannot rebind itself,
the current owner should quiesce and `release_producer()` before launching it;
its first admitted frame then claims ownership. LVGL `start()` binds automatically.

Before changing rotation or handing the presenter to another renderer, pause
production and `quiesce()`; for LVGL, stop the binding first as described in the
[binding guide](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md).
After rotation, query the layout again. Stop/destroy a renderer binding before
deleting its presenter, then release the application-owned panel/IO resources.

A timeout or cancellation does not prove that hardware has stopped reading.
If stop/delete cannot complete, retain the object and borrowed storage for a
retry; do not free them on that error path. A latched fault requires safe teardown
and recreation; query `get_fault_error()` for the original cause. Hardware
acceleration is optional and depends on the SDK, SoC and pixel layout. See [ownership and recovery](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/STATE_MACHINE.md).

## Examples and reference

- [LVGL benchmark](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark): panel setup and LVGL integration.
- [GSP/LVGL handoff](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff): producer ownership and retryable recovery.
- [Compatibility and migration](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md), [TE timing](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md), [public API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h).
- [Host regression and target smoke](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md): maintained test entry points.
