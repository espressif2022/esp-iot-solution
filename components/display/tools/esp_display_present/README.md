# ESP Display Present

[中文](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README_CN.md)

`esp_display_present` manages frame submission, buffer ownership and display synchronization for ESP LCD panels. The application initializes the panel and bus; Present manages the lifetime of the pixel work submitted by a renderer.

Connect LVGL 9 through [esp_lv_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md). Custom renderers use the [Presenter API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h).

## Choose a mode and pipeline combination

Choose the panel and rendering path first, then configure buffer counts. Mode names below omit the `ESP_DISPLAY_PRESENT_MODE_` prefix.

| Panel and use | Mode | Renderer input |
|---|---|---|
| Panel IO, ordinary area updates | `NONE` | PARTITION: submit areas in tiles |
| Panel IO, transmission within TE windows | `TE_SYNC` | PARTITION: compose tiles, then send a complete image |
| RGB/MIPI, tiled rendering | `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | PARTITION: copy tiles into a candidate framebuffer |
| RGB/MIPI, complete redraw each frame | `DOUBLE_FULL` / `TRIPLE_FULL` | FULL: submit a complete frame |
| RGB/MIPI, dirty updates within a complete image | `DOUBLE_DIRECT` | DIRECT: submit dirty regions in a full-frame buffer |

`AUTO` resolves as follows:

- RGB/MIPI: `TRIPLE_PARTIAL` with three driver framebuffers, `DOUBLE_PARTIAL` with two.
- Panel IO: `TE_SYNC` with a valid TE configuration, otherwise `NONE`.

**A zero-initialized mode selects `NONE`, not `AUTO`.** RGB/MIPI do not support `NONE` or `TE_SYNC`. Panel IO does not support the FB FULL, DIRECT or PARTIAL modes.

## Requirements and dependencies

- ESP-IDF 6.0 or newer; RGB565 or RGB888 input matching the initialized panel configuration.
- RGB/MIPI require at least two driver framebuffers and a supported scanout-retirement protocol. DSI additionally needs DPI frame-complete and VSYNC callbacks, which ESP-IDF 6.0 does not provide.
- Panel IO requires both panel and IO handles. The current transfer adapter implements SPI/QSPI completion-callback semantics.

Enable driver bounce buffers for ESP32-S3 RGB. RGB uses continuous refresh and untransformed driver coordinates; apply logical rotation through Present. When an older driver cannot report its actual configuration, use the [Kconfig compatibility option](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/Kconfig) only for a panel actually configured with bounce buffers. See [mode and output adaptation](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md) for other scanout protocols.

To use this checkout, add the dependency to the application's `main/idf_component.yml` and replace the path:

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

A source version declaration does not establish Registry availability. LVGL users must select both local components as shown in the [binding guide](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md).

## Create a presenter

### RGB/MIPI: borrow the driver framebuffers

Initialize the panel through the BSP or `esp_lcd`, then create the presenter:

```c
#include "esp_display_present.h"

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

Use `ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI` for MIPI. Before initializing the driver, `esp_display_present_get_required_frame_buffer_count()` can query the recommended framebuffer count; it allocates no storage.

### Panel IO: two internal draw buffers and background output

This example configures ordinary SPI/QSPI RGB565 updates. Set `swap_bytes` to match the wire byte order required by the panel driver.

```c
#include "esp_display_present.h"

esp_err_t create_io_presenter(esp_lcd_panel_handle_t panel,
                              esp_lcd_panel_io_handle_t io,
                              esp_display_presenter_t **out_presenter)
{
    const esp_display_presenter_config_t config = {
        .width = 480,
        .height = 480,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .target = {
            .hw = {
                .panel = panel,
                .io = io,
                .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .swap_bytes = true,
            },
            .fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE,
            .drawbuf = {.lines = 32, .buffers = 2, .in_psram = false},
            .io_submit = ESP_DISPLAY_PRESENT_IO_SUBMIT_BACKGROUND,
        },
    };
    return esp_display_presenter_create(&config, out_presenter);
}
```

| Entry point | Dimensions | Use |
|---|---|---|
| `open()` | Physical width and height | Default buffer policy |
| `open_ex()` | Physical width and height | Custom tile rows/count, PSRAM preference and TE compose count |
| `create()` | Logical width and height | Also configure `io_submit`, timeouts, damage limits and explicit framebuffers |

Set rotation through `hw.rotation`. After creation, query `get_caps()` for the render contract and `get_effective_config()` for the actual mode, buffer counts, memory domains and degradation flags. Keep panel, IO and driver storage alive until presenter deletion succeeds; Present manages its registered completion callbacks during that lifetime.

## Panel IO NONE: rendering and transmission

**D is the renderer's draw buffer; S is an internal DMA staging buffer for a band of pixels.** The three profiles below are recommended starting points. D count (1/2) and memory domain can also be selected independently.

| Profile | `io_submit` | `drawbuf.buffers` | `drawbuf.in_psram` | Present background tasks |
|---|---|---|---|---|
| Simple small tiles, minimum task overhead | `CALLER` | 1 | `false` | 0 |
| Ordinary tiles, overlapping rendering and output | `BACKGROUND` | 2 | `false` | 1 backend |
| Large tiles/full-screen D, conserving internal draw memory | `BACKGROUND` | 2 | `true` | 1 backend |

`CALLER` and `BACKGROUND` omit the `ESP_DISPLAY_PRESENT_IO_SUBMIT_` prefix. `DEFAULT` equals `CALLER`; `open()` and `open_ex()` use `DEFAULT`. Only `NONE` accepts an explicit submission policy.

CALLER processes pixels and submits on the calling task, which may block. BACKGROUND moves this work to one backend task. Swap, rotation and D memory location never add a pixel task.

### Output selection for each submission

| Input conditions | Pixel processing | DMA source |
|---|---|---|
| 0°, packed rows, actual address and length satisfy DMA constraints | No copy; apply in-place RGB565 swap16 when required, then cache writeback | D |
| Rotation, padded source rows, or source unsuitable for direct DMA | Read D and fuse copy/rotation/swap into physical rows in S | S |

Allocate two S buffers on first use, each with a default budget of at most 16 KiB. Fill the next slot while the current band sends, with at most one DMA band in flight. The tail sends real pixels without expanding damage or padding wire data to DMA alignment. **Internal staging capacity does not grow with D; there is no input-sized conversion pool.**

Direct PSRAM DMA requires matching SoC and IO support plus a legal actual address and length for this request; otherwise use S. When the driver cannot report its capability, `hw.spi_psram_dma_direct` must match the actual IO creation flag. RGB565 byte swapping and RGB/BGR component order are separate settings.

### When D can be drawn again

```text
Render D → submit → send D directly, or send bands through S
                              ↓
                    complete the entire area → D can be acquired again
```

Function return does not establish DMA completion. Do not write D after submission, and do not overwrite S before its own DMA completes. Two D buffers allow rendering into the other buffer; slower output still creates backpressure.

D may cover the full screen, for example with `drawbuf.lines = height`. It still uses PARTITION leases, so do not rely on retained content across frames. Two 480×480 RGB565 D buffers occupy 900 KiB, excluding S, metadata and task stacks.

## Panel IO TE_SYNC: compose first, then send within a window

TE composes D into physical full-frame images C before admitting output against TE timing. D and C counts are independent:

| D count | `te_compose_buffers` | Compose images | Present background tasks |
|---|---|---|---|
| 1 or 2 | 0 / 1 | 1 C | 1 backend, also performing pixel composition |
| 1 or 2 | 2 | 2 C | 1 backend + 1 pixel task |

One C must wait for its transmission to finish before being rewritten. Two allow composition of the next image while the previous one sends. Rotation writes directly into C, without another rotation frame.

C prefers PSRAM and may fall back to internal DMA memory. Internal DMA C or confirmed direct-DMA PSRAM C submits directly. Non-direct PSRAM C adds two S buffers at creation, using the same byte-budget setting as NONE but separate buffers per instance.

First enable and wire TE output in the panel driver. Set `hw.te_enabled`, `hw.te_sync.gpio_num`, `bus_freq_hz` and `data_lines` to match the hardware. Bus parameters seed the first estimate; subsequent admission uses measured timing, including band gaps. Startup polarity detection assumes short blanking pulses and a longer idle level; other waveforms need validation. **TE configuration and buffer counts alone cannot guarantee tear-free output.** See [TE timing and admission](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md).

## RGB/MIPI: frame construction and scanout retirement

The driver owns two or three framebuffers (FBs). Present buffers below are additional to those FBs. RGB/MIPI use native pixel byte order without software RGB565 swap.

| Mode and rotation | Additional Present pixel buffers | Present background tasks |
|---|---|---|
| FULL / DIRECT, 0° | None; render directly into driver FBs | 1 backend |
| FULL / DIRECT, 90° / 180° / 270° | 1 shared logical full-frame canvas | 1 backend, also processing pixels |
| PARTIAL, any rotation, one D | 1 D, copied/rotated into the candidate FB | 1 backend, also processing pixels |
| PARTIAL, any rotation, two D | 2 D, copied/rotated into the candidate FB | 1 backend + 1 pixel task |

FULL means `DOUBLE_FULL` / `TRIPLE_FULL`; DIRECT means `DOUBLE_DIRECT`; PARTIAL means `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`. Rotated FULL/DIRECT share one logical canvas, preferring PSRAM and releasing it on return to 0°. PARTIAL adds no logical full-frame canvas.

**Copy completion and scanout retirement are different events.** D can return after its copy completes; a driver FB must retire through the scanout protocol before it can be rendered again. Background-task counts exclude application/LVGL tasks, driver tasks, ISRs and timer callbacks. Acceleration depends on SDK, SoC and layout; background pixel work may also use the CPU.

## Shared buffer and task configuration

Use `target.drawbuf` with `create()`, or the `drawbuf` argument of `open_ex()`:

| Field | Meaning |
|---|---|
| `lines` | Logical rows per D; zero uses the default tile-byte target, with at least one row and at most the logical height |
| `buffers` | D count: 1 or 2; zero selects automatically, and only an automatically selected pair may fall back to one after allocation failure |
| `in_psram` | Prefer PSRAM for D; actual allocation may fall back to internal DMA memory |
| `te_compose_buffers` | TE_SYNC only: 0/1 selects one C, 2 selects two |

FULL/DIRECT do not use the input tile pool. A 480×32 RGB565 D costs 30 KiB. Frontend-owned buffers, driver bounce buffers, DMA descriptors, alignment, stacks and metadata cost extra. `get_effective_config()` does not enumerate every lazy allocation.

The `Display presenter` menu in menuconfig provides:

| Kconfig | Default | Purpose |
|---|---|---|
| `CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES` | 32 KiB | D byte target when `lines = 0`; a single row may exceed the target |
| `CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES` | 16 KiB | Byte limit per S, rounded to physical whole rows; reject a path whose physical row exceeds it |
| `CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY` | 0 | Backend priority: zero snapshots the current producer priority at creation/successful binding; nonzero fixes the application-selected priority |

NONE and TE share the S budget setting, with separate buffers per instance. `sdkconfig.rename` migrates the former TE setting. Backend priority does not change per submission, completion or scene; dedicated pixel tasks retain their first submitter's priority. Priority and buffer counts do not replace scheduling and bandwidth validation.

## Render, hand off and release

The basic custom-renderer sequence is:

```text
begin_next_frame()
    → acquire_buffer() → render → submit_buffer()   // repeat for PARTITION
    → commit_frame()
```

Follow the returned contract, stride and lease capacity. The lease's `resolve_rows` callback computes how many rows fit the chosen stride; never exceed `capacity_bytes` or write after submission. The LVGL binding performs these operations for LVGL.

Renderer calls belong to the presenter's producer task. Creation initially binds the calling task. To move a custom renderer, call `rebind_producer()` between frames. If the next adapter cannot rebind itself, the current owner should quiesce and `release_producer()` before starting it. LVGL `start()` binds automatically.

Pause production and `quiesce()` before rotation or renderer changes; stop the LVGL binding first as described in its guide. Quarter-turns exchange logical dimensions, so query the layout again afterwards. NONE rotation adds no tasks or input-sized conversion pool; see the relevant tables for other modes' canvas changes.

Error handling and shutdown rules:

- A rejected NONE CALLER submission retains the original lease and pixels for an explicit retry after correcting the cause.
- Staging OOM fails an already accepted NONE worker request with `NO_MEM` and safely returns D. Driver backpressure waits for genuine completion without busy retries.
- Timeout or cancellation does not establish hardware retirement. If stop/delete cannot complete, retain the object and borrowed storage for retry. A latched fault requires safe teardown and recreation; use `get_fault_error()` for the original error.
- Stop/destroy the renderer binding before deleting the presenter. Release application-owned panel and IO resources only after deletion succeeds.

## Examples and reference

- [LVGL benchmark](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark): panel initialization and LVGL integration.
- [GSP/LVGL handoff](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff): producer handoff and retryable recovery.
- [Mode and buffer contracts](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md): mode mapping, output protocols and internal responsibilities.
- [Ownership and recovery](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/STATE_MACHINE.md), [compatibility and migration](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md).
- [Host regression and target smoke](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md): maintained test entry points; performance evidence must identify its revision and board configuration.
