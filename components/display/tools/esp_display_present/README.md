# ESP Display Present

English | [中文](README_CN.md)

`esp_display_present` provides a common display output interface for LVGL, GSP,
and custom renderers. Built on `esp_lcd`, it manages display buffers, pixel
transforms, and presentation synchronization to suit different panels and refresh needs.

```text
LVGL 9 → esp_lv_present ──┐
GSP / custom renderer ────┤
                         ▼
                esp_display_present
          Buffer management · Rotation · Sync
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
        SPI / QSPI              RGB / MIPI DSI
      Send pixels to panel      Switch framebuffers
```

The application initializes the bus and panel. The renderer produces pixels.
Present delivers the image to the display.

## Capabilities

- **Multiple refresh methods**: partition rendering, full redraws, and dirty-area updates in a complete canvas.
- **Display synchronization**: TE synchronization for Panel IO and double/triple framebuffer switching for RGB/MIPI.
- **Pixel transforms**: RGB565, RGB888, 0°/90°/180°/270° rotation, and RGB565 byte swapping for Panel IO.
- **Memory and pipeline configuration**: prefer internal memory or PSRAM for draw buffers and use buffer counts to overlap rendering and output. Panel IO uses bounded intermediate storage when needed, including for large or screen-sized input buffers.

## Choose a mode

Choose by panel interface and how the application renders. Names below omit
the `ESP_DISPLAY_PRESENT_MODE_` prefix.

| Panel interface | Use case | Mode | Main memory requirements |
|---|---|---|---|
| SPI/QSPI | Render and send tiles | `NONE` | 1/2 draw buffers; intermediate storage as needed |
| SPI/QSPI + TE | Compose complete images and send within TE windows | `TE_SYNC` | Draw buffers + 1/2 compose images; intermediate storage as needed |
| RGB/MIPI DSI | Update dirty areas through small draw buffers | `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | Draw buffers + 2/3 driver framebuffers |
| RGB/MIPI DSI | Redraw the complete image each frame | `DOUBLE_FULL` / `TRIPLE_FULL` | 2/3 driver framebuffers |
| RGB/MIPI DSI | Update dirty areas in a complete canvas | `DOUBLE_DIRECT` | 2 driver framebuffers |

Rotated FULL/DIRECT modes add one shared logical canvas. Panel IO retains its
partition submission interface even when a draw buffer holds a complete screen.

`NONE` provides no tearing avoidance. TE requires a panel TE signal and timing
matched to the actual transfer. RGB/MIPI separate rendering and scanning through
framebuffer handoff.

Explicitly setting `AUTO` selects `TE_SYNC` or `NONE` for Panel IO according to TE
configuration. RGB/MIPI select `DOUBLE_PARTIAL` or `TRIPLE_PARTIAL` according to
the actual driver framebuffer count. A zero-initialized mode is `NONE`, which
RGB/MIPI do not support.

### Buffers and background tasks

- **D (Draw buffer)**: renderer storage for a tile or band. Use one or two, with a preference for internal memory or PSRAM.
- **C (Compose buffer)**: a complete image assembled for TE output. Use one or two.
- **F (Framebuffer)**: a complete RGB/MIPI driver scanout image. The mode selects two or three.
- **S (Staging buffer)**: internal DMA storage used when Panel IO needs conversion or cannot transfer directly. There are two slots, each bounded to 16 KiB by default.

D count is independent of C/F count. For example, `2 D + 3 F` means two draw
buffers and three driver framebuffers, not five screen-sized canvases.
FULL/DIRECT draw complete surfaces and do not use a D pool.

The mode and actual buffer counts determine background tasks automatically.
`backend` advances submission and completion handling; a separate `pixel` task
performs copies, rotation, and composition. Counts below exclude GUI, driver,
and system tasks.

| Mode and buffers | Background tasks | Work distribution and overlap |
|---|---|---|
| `NONE`: 1 D | 0 | Caller processes and submits pixels; D becomes drawable again after transfer completion |
| `NONE`: 2 D | 1 backend | Backend processes and sends D0 while the application can render D1 |
| `TE_SYNC`: 1/2 D + 1 C | 1 backend | Backend handles composition and output; the single C alternates between composition and transfer |
| `TE_SYNC`: 1/2 D + 2 C | backend + pixel, 2 total | Pixel composes the next C while backend sends the previous C |
| `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`: 1 D + 2/3 F | 1 backend | Copy/rotate into F, then advance switching; synchronous single-D copies are described below |
| `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`: 2 D + 2/3 F | backend + pixel, 2 total | Application renders D1, pixel processes D0, and the display scans a submitted F |
| FULL/DIRECT: 2/3 F, plus a logical canvas when rotated | 1 backend | Application draws a complete surface; conversion follows the selected path and backend advances switching; DIRECT uses only 2 F |

**Synchronous and asynchronous processing**: single-D FB PARTIAL copies inline
in the caller when the selected executor is CPU and rotation is 0°/180°.
Other pixel requests run in the background. FULL also has an inline CPU path
for 180° full-frame conversion. A backend task does not imply every pixel
operation is asynchronous; having no background task does not make DMA synchronous.

**Configuration**: `drawbuf.buffers` selects D count,
`drawbuf.te_compose_buffers` selects C count, and `DOUBLE_*`/`TRIPLE_*` selects
F count. Automatic allocation may reduce D count under memory pressure; query
`get_effective_config()` for the result. In `NONE`, rotation, byte swapping,
and PSRAM staging do not add a pixel task.

## Requirements and dependencies

- ESP-IDF 6.0 or later, with pixel format matching the initialized panel.
- The current Panel IO adapter supports the SPI/QSPI completion callback protocol; other interfaces require separate adaptation.
- RGB/MIPI require at least two driver framebuffers. ESP32-S3 RGB requires driver bounce buffers. On ESP-IDF 6.0 MIPI-DSI (`on_refresh_done` only), `CONFIG_ESP_REV_MIN_FULL >= 300` waits one extra refresh period per multi-buffer switch to guarantee DMA retirement; ESP-IDF 6.1+ uses `on_frame_buf_complete` directly.

Use this source version as a local component in the application's `main/idf_component.yml`:

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

A source version does not imply Registry publication. LVGL users must also select
the matching local `esp_lv_present` as described in its [binding guide](../esp_lv_present/README.md).

## Create a presenter

Initialize the panel first, then create a Presenter. For an RGB panel with automatic mode selection:

```c
#include "esp_display_present.h"

const esp_display_presenter_panel_config_t lcd = {
    .width = 480,
    .height = 480,
    .hw = {
        .panel = panel,  // Initialized panel handle
        .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
        .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
    },
};
esp_display_presenter_t *presenter = NULL;
ESP_ERROR_CHECK(esp_display_presenter_open(
    &lcd, ESP_DISPLAY_PRESENT_MODE_AUTO, &presenter));
```

Panel IO also needs `.hw.io`, the `PANEL_IO` type, and the appropriate byte-order
and TE settings. Use `open_ex()` to configure draw-buffer size, count, and memory
preference. See the [public API](include/esp_display_present.h) and
[configuration header](include/esp_display_present_config.h) for all fields.

**LVGL 9**: bind through [esp_lv_present](../esp_lv_present/README.md); the application runs the UI task.
**GSP / custom renderers**: acquire storage, draw, and submit images through the public API.

```text
Acquire drawing storage → Render → Submit image
          ↑                             │
          └──────── Next frame ─────────┘
```

## Examples and further reading

- [LVGL benchmark](../../../../examples/display/gui/lvgl_present_benchmark/README.md): panel initialization, LVGL integration, and performance testing.
- [GSP/LVGL handoff](../../../../examples/display/gui/gsp_lvgl_present_handoff/README.md): share display output between renderers.
- [Modes and buffers](docs/MODE_ARCHITECTURE.md), [TE configuration and timing](docs/TE_TIMING.md), and [compatibility and migration](COMPATIBILITY.md).
- [Host tests](tests/host/README.md) and [internal state machine](STATE_MACHINE.md): validation and maintenance references.
