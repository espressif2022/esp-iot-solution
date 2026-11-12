# Changelog

## 0.2.0 — Unreleased

- Bind to esp_display_present 2.x and the single presentation engine.
- Preserve the binding entry points while requiring the present 2.x ownership
  and scanout policies. Do not resolve this release against present 1.x.
- See the sibling component's COMPATIBILITY.md for the source migration boundary.

## 0.1.2 - 2026-10-09

- Fix borrowed buffer alignment, capacity, color format and stride.

## 0.1.1 - 2026-09-30

- Scope IDF atomic-helper warning compatibility to LVGL's FreeRTOS source so
  standalone presenter examples do not need the full LVGL adapter.

## 0.1.0 - 2026-09-04

- Host the component in esp-iot-solution under `components/display/tools/esp_lv_present`.
- Relicense the component to Apache-2.0.
- Add a Unity test app and point examples at `lvgl_present_benchmark` and
  `gsp_lvgl_present_handoff`.
