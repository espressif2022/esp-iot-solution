| Supported Targets | ESP32-P4 | ESP32-S3 | ESP32-S31 | ESP32-C3 |
| ----------------- | -------- | -------- | --------- | -------- |

# LVGL Present Benchmark

On-target `lv_demo_benchmark` over an app-owned `esp_display_present`
presenter. Use this to compare FPS against `esp_lvgl_adapter`'s
`lvgl_common_demo` with matching presenter settings.

This example does not start GSP. LVGL is bound through `esp_lv_present`.

## Runtime matrix

The example does not select a presentation mode or rotation at compile time. It
derives a runtime matrix from the initialized panel type, TE wiring and known
mode constraints. Each combination creates a fresh presenter and LVGL display,
runs `lv_demo_benchmark` through its final summary callback (bounded by
`CONFIG_APP_LVGL_BENCH_TIMEOUT_SECONDS`), then performs:

```text
quiesce presenter -> stop LVGL -> delete presenter -> next combination
```

RGB/MIPI targets run both FULL modes, `DOUBLE_DIRECT`, both PARTIAL modes,
and `AUTO`; `NONE` is unsupported and logged as `SKIP`. GRAM targets run `NONE`, `AUTO`, and `TE_SYNC` with both one
and two compose buffers when TE is wired. Inapplicable combinations are logged
as `SKIP`; driver-rejected configurations are reported separately. By default,
the worker stops after one complete matrix. Enable
`CONFIG_APP_LVGL_BENCH_LOOP` to repeat the matrix continuously.

## Build / flash

The panel is selected by the `Hardware Configuration` Kconfig choice, same as
other GUI examples. LCD bring-up reuses `examples/display/gui/common/hw_init`.
Target-specific PSRAM/XIP/cache live in `sdkconfig.defaults.esp32s31` /
`sdkconfig.defaults.esp32p4`. Requires ESP-IDF >= 6.0.

```sh
# ESP32-S31 (RGB, 250M PSRAM + XIP)
idf.py set-target esp32s31 build flash monitor

# ESP32-P4 (MIPI-DSI, HEX PSRAM + L2)
idf.py set-target esp32p4 build flash monitor
```

## Expected serial output

```text
I (...) lvgl_bench: matrix round=1 start
I (...) lvgl_bench: RUN mode=DOUBLE_DIRECT te_buffers=0 rotation=0 size=1024x600 timeout=120s
I (...) lvgl_bench: PASS mode=DOUBLE_DIRECT te_buffers=0 rotation=0 frames=... transport_fps=... lvgl_fps=... cpu=... render=...ms flush=...ms scenes=...
I (...) lvgl_bench: SKIP mode=TE_SYNC te_buffers=1 rotation=0: outside target capability matrix
```


### Performance counters

Each successful case emits a `METRICS` line. `begin_commit_p99_us` measures
successful producer frames from begin admission to commit. It includes waits
for tiles during the frame and uses 1 ms histogram buckets. If P99 lands in
the overflow bucket, the line reports the observed maximum as its upper bound.

`buffer_waits`, `buffer_wait_us`, `buffer_wait_max_us` and
`buffer_wait_timeouts` describe backpressure during frame or tile acquisition.
Gate-only contention is excluded. `scanout_supply_misses` counts VSYNC intervals
that started with a committed framebuffer backlog and received no matched
scanout switch. It is available for RGB/DPI when `scanout_available=1`.
Physical LCD DMA underruns and restarts require separate driver counters.
