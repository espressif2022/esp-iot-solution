# ESP Display Present

`esp_display_present` provides the stable `esp_display_presenter_*` API, platform resource assembly, and one presentation engine. `esp_lv_present` provides the LVGL binding to that engine. Existing applications keep their presenter API and own the panel, IO, and bus resources.

Public headers are in `include`; internal engine headers in `src` are not application API. The previously public geometry header and its point/size types are no longer exposed; consumers of those interfaces require migration.

For local component discovery add `esp_display_present` and, for LVGL, `esp_lv_present`; neither requires a V2 component. Registry publication and external compatibility have not been established by this source migration. See [current development and validation](MIGRATION_STATUS.md) and the [historical transition design](../esp_display_present_v2/TRANSITION_DESIGN.md).

ESP32-S3 enables `CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM` by default to place framebuffer engine and pixel-copy hot paths in internal RAM. With RGB bounce buffers and a 16 KiB instruction cache, the measured double-FB PARTIAL and DIRECT throughput returns to approximately 60 submissions/s, at about 19 KiB of additional IRAM in that build. Memory-constrained applications can disable it and reassess framebuffer throughput. This placement does not change buffer retirement or make task APIs ISR-safe or cache-disabled-safe. See [test conditions and limits](dev/FB_IRAM_RESULTS_20261103.md).

`CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND` optionally moves synchronous internal-tile GRAM submissions to the existing engine sender, releasing the engine gate during driver calls without forcing a pixel copy. It is disabled by default; rotated and PSRAM tile paths keep their existing scheduling. `CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY` sets the experimental sender priority, defaulting to 7. See [conditions, RAM cost, and measured results](dev/GRAM_BACKGROUND_RESULTS_20261104.md).

See [TE timing and scheduling](docs/TE_TIMING.md) for automatic measurement, the shared window model, and compatibility boundaries.

See [current board benchmarks and host validation](dev/VALIDATION.md) for the tested commit, configurations, scene results, and limits.

The [mode and rotation matrix](dev/MODE_ROTATION_RESULTS_20261103.md) records an unsafe runtime cross-axis rotation through the public API on the non-square P4 panel, and a runtime rotation limitation in S31 GRAM. These issues remain open.

RGB framebuffer modes distinguish periodic bounce completion from requested
link-switch completion. The zero-initialized `target.hw.rgb.scanout` retains
existing driver-query / confirmed-bounce behavior. For an ESP32-S31 panel created
with `bounce_buffer_size_px = 0`, explicitly declare:

```c
config.target.hw.rgb.scanout = ESP_DISPLAY_PRESENT_RGB_SCANOUT_LINK_SWITCH;
```

This declaration does not change panel settings. It requires continuous refresh,
exclusive panel/callback ownership, driver-owned framebuffers, no driver
rotation/mirror or custom bounce fill, and the audited S31 AXI link-switch driver
in clean IDF 6.2. Only one switch may be outstanding. Early completion is retained
until the driver call succeeds; periodic completion keeps its ISR-join fence.
S3 non-bounce EOF is not accepted as a retirement guarantee. No IDF patch is
required. See [RGB handoff regression](dev/test_apps/rgb_handoff/README.md).

Test projects, board profiles, LVGL benchmark and adapter comparison: [dev/README.md](dev/README.md).
