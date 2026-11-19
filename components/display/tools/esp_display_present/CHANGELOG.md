# Changelog

## 2.0.0 — Unreleased

- Execute unrotated single-tile FB copies inline when the selected executor is
  CPU-only. Keep repair asynchronous and preserve queued hardware/custom
  executors, multi-tile overlap and scanout retirement.

- Remove the public `target.io_submit` field and `esp_display_present_io_submit_t`.
  Panel-IO NONE now derives submission context from the final input count: one
  buffer uses the caller, two use one backend worker. Automatic 2->1 allocation
  fallback also recomputes the worker and stack budget. Existing initializers
  must remove `.io_submit`; TE/framebuffer scheduling and DMA retirement are unchanged.

- Fail worker-owned Panel IO requests on staging OOM and safely return their D
  buffers. Distinguish driver backpressure from allocation failure, wait for
  genuine completion to retry, and avoid rejected submissions self-waking in a
  busy loop. CALLER rejection retains its original lease for explicit retry.

- Converge non-TE Panel IO on CALLER or one BACKGROUND output worker. Choose
  direct DMA per request (including in-place RGB565 swap), otherwise generate
  physical rotated/swapped bands in two bounded internal staging slots. Remove
  REGION pixel workers and input-sized conversion pools; retain D until logical
  transfer completion. PSRAM full-screen D uses the same PARTITION contract.
- Share IO_STAGING_SLOT_BYTES between NONE and TE, migrating the old TE Kconfig
  name; NONE allocates staging lazily and input count selects its submission context.
- Run single-image TE composition on the existing backend worker while
  retaining asynchronous copy ownership; dual-image TE keeps its pixel worker.
- Detect TE short-pulse polarity after enabling the input, using a bounded
  startup sample instead of a single level that may land inside the pulse.
- Prioritize dual-image TE output over its pixel worker to reduce bounded
  DMA staging refill gaps during concurrent rendering.
- Configure the default tile-byte target and per-slot TE staging limit through
  Kconfig, with shared factory/plan rounding rules and explicit tile-row overrides.

- Declare the public geometry/scanout compatibility break explicitly; see
  `COMPATIBILITY.md` before upgrading from 1.x.
- Detect optional SDK acceleration/DPI callback APIs during configuration;
  unavailable executors use the existing CPU fallback, while unavailable DSI
  scanout returns `ESP_ERR_NOT_SUPPORTED` rather than reinterpreting refresh events.

- Fix fault classification at the presenter facade: only an original
  `ESP_ERR_TIMEOUT` is reported as `ESP_DISPLAY_PRESENT_FAULT_TRANSFER_TIMEOUT`.
  Other accepted transfer/copy failures now report the new
  `ESP_DISPLAY_PRESENT_FAULT_TRANSFER_FAILED`, and the new
  `esp_display_presenter_get_fault_error()` returns the original error before
  classification. This changes the reported reason for non-timeout failures;
  applications must not treat the new reason as a timeout. The classification
  contract and matrix are documented in `STATE_MACHINE.md` and covered by
  `fault_classification_matrix` in `tests/host/test_legacy_facade.c`.
- Keep the main presenter API and component name, backed by one internal engine.
- Remove the superseded implementation and private-source dependencies.
- Require multi-framebuffer scanout for RGB/MIPI; reject NONE and single-FB
  configurations. AUTO selects double/triple PARTIAL against the actual pool.
- Remove full-screen placeholder canvases for LVGL FULL/native DIRECT.
- Move the former public geometry interfaces to internal headers; consumers of
  those interfaces require migration.
- See `COMPATIBILITY.md`, `docs/MODE_ARCHITECTURE.md` and `STATE_MACHINE.md`
  for current contracts.

## 1.2.2 - 2026-11-10

- Fix PPA rotation buffer alignment, capacity, strides and dimension checks.
- Fix TE input setup and automatic polarity detection.

## 1.2.1 - 2026-11-09

- Bound GRAM draw-buffer acquisition to the transfer timeout, retain DMA-owned buffers
  when completion callbacks are missing, and expose the resulting presenter fault.

## 1.2.0 - 2026-10-27

- Add correlated frame receipts, coherent transfer/display status and tracking cancellation.
- Track RGB/MIPI VSYNC on demand and preserve frame IDs across rotation.
- Wake TE at phase boundaries on IDF 6.2+, with callback-safe teardown and edge/tick fallback.

## 1.1.2 - 2026-10-09

- Align renderer buffers and expose their guaranteed base alignment.
- Fix framebuffer retirement races and early RGB completion.

## 1.1.1 - 2026-09-30

- Add a hardware-free panel framebuffer requirement query that shares mode,
  rotation and TE policy validation with presenter creation.
- Remove the LVGL adapter dependency from presenter examples and size the
  benchmark's panel buffers for its supported mode and rotation matrix.
- Recheck TE phase eligibility when a current-scan transfer window opens,
  avoiding an unnecessary wait for the next GPIO edge after fast rendering.
  Keep the existing phase-window bounds and revalidate every wakeup.

## 1.1.0 - 2026-09-28

- Overlap partition and TE tile copies with rendering when a second draw buffer
  is available; worker allocation falls back to synchronous copies.
- Fix RGB panels locking fast producers to every other refresh.
- Speed up `DOUBLE_DIRECT` damage sync and the RGB565 byte swap.

## 1.0.2 - 2026-09-04

- Host the component in esp-iot-solution under `components/display/tools/esp_display_present`.
- Relicense the component to Apache-2.0 to match other esp-iot-solution display tools.
- Add the LVGL present benchmark example and a Unity test app.
- Move the GSP/LVGL presenter handoff into ``examples/display/gui/gsp_lvgl_present_handoff``.
- Add ``esp_lv_present`` as the LVGL binding for this presenter.

## 1.0.1 - 2026-09-03

- Rework TE-synchronized presentation around an inline double-buffer pipeline,
  allowing rendering to overlap an in-flight panel transfer without a worker
  task.
- Share buffer repair, pooled-buffer ownership, and transfer-fence handling
  across framebuffer and TE presentation paths.
- Make presenter quiesce wait safely for the final transfer-complete ISR, which
  prevents Display Off from deadlocking while a frame is in flight.
- Remove the obsolete `te_inline_submit` configuration flag; TE composition now
  uses the inline submission path directly.
- Add an on-target runtime benchmark matrix for every panel-compatible
  presentation mode and rotation, including the LVGL DIRECT contract.
- Harden the GSP/LVGL handoff stress app with bounded transition retries,
  zero-frame detection, and fail-closed presenter ownership.

## 1.0.0 - 2026-08-27

- Initial stable release of the renderer-independent ESP LCD presentation and
  tearing-control component.
