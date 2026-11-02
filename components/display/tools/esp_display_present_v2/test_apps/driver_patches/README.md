# RGB driver capability query

`0001-rgb-readonly-frame-buffer-info.patch` extends the local ESP-IDF RGB driver with a public, read-only query. It was prepared against IDF commit `7b9cc1ac79f` (6.2 development). This is a local SDK extension, not an API already released upstream.

Apply in the IDF checkout with `git apply --check <patch>` followed by `git apply <patch>`. A checkout already containing this patch should pass `git apply --reverse --check <patch>` instead.

`esp_lcd_rgb_panel_get_frame_buffer_info()` reports actual dimensions, valid frame-buffer capacity, buffer count, bounce capacity, continuous refresh, custom bounce callback, transformations, and reliable retirement support. The query snapshots driver state under its lock and neither submits a transfer nor changes callbacks, buffers, or DMA state. As with other panel getters, callers must serialize panel deletion and configuration changes.

The facade consumes these facts without changing application calls. A caller-provided bounce hint cannot override contradictory driver facts. On an unpatched SDK, the explicit legacy confirmation remains the fallback; automatic discovery requires this extension.

This does not make continuous, multiple-buffer RGB without bounce safe on S3: its EOF event does not prove that preloaded reads have ended. Unsupported retirement configurations remain rejected. Single-buffer active writes are a separate, explicit policy and do not establish retirement of another buffer.

## SPI IO memory path

`0002-spi-readonly-memory-info.patch` adds `esp_lcd_panel_io_spi_get_memory_info()` to the same SDK baseline. It reports the immutable `psram_dma_direct` configuration without starting DMA or changing callbacks. It verifies the SPI implementation before reading implementation storage and returns NOT_SUPPORTED for other IO types. Callers serialize IO deletion.

The facade uses this fact to avoid staging when the driver already enables direct PSRAM DMA. Address/capacity/cache-alignment admission still runs in the SPI adapter; this flag is not blanket permission for any external pointer. Unknown IO or SDKs without this query retain staging. The common executor and backend lifecycle have no SoC branch added by this query.
