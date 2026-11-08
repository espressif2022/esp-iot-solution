# RGB / MIPI DSI NONE partition path

Date: 2026-11-08

## Change

Public `NONE` now has one ownership model for RGB and MIPI DSI. Present owns a
bounded PARTITION tile pool and submits dirty rectangles with
`esp_lcd_panel_draw_bitmap()`. The LCD driver owns its framebuffer. The source
tile is released only after the matching RGB or DPI `on_color_trans_done`
callback, and callback removal is followed by the existing ISR barrier before
adapter storage is destroyed.

Explicit `DOUBLE_FULL`, `TRIPLE_FULL`, and `DOUBLE_DIRECT` modes continue to use
the scanout framebuffer adapter. This change does not alter those modes.

## Verification

- Host tests: 42/42 passed, including both RGB and DSI `NONE` resource,
  rotation, submit, source-release, stop, and destroy paths.
- ESP32-P4 MIPI DSI benchmark firmware built successfully for `NONE`, 0° with
  IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`.
- ESP32-S3 RGB benchmark firmware built successfully for `NONE`, 0° with the
  same IDF.
- No P4 board was connected in this run. The old DSI `NONE` FPS and memory rows
  describe the removed DIRECT path and must not be reported as results for the
  new PARTITION implementation.
