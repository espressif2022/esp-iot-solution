# ESP LV Present

`esp_lv_present` provides the stable LVGL 9 integration and native binding for the single engine in `esp_display_present`. The stable `esp_lv_present_*` application interface remains available. Public LVGL extension headers are under `include/advanced`; their long-term boundary is under review.

Use local `esp_lv_present`, `esp_display_present`, and LVGL 9.0 or newer within major 9. LVGL 9.0–9.4 uses an independent draw buffer and a copy into the presenter; native pooled and FULL zero-copy creation return `ESP_ERR_NOT_SUPPORTED` on these releases. See [development and validation](../esp_display_present/MIGRATION_STATUS.md). Source migration is not a Registry release.

When using Component Manager with these local sources, explicitly select **both** local component paths in the application manifest. The current presenter version constraint also matches an older Registry package that does not contain the private bridge required by this binding. Do not publish these manifests unchanged; the repository contains the tested setup and remaining version work in `esp_display_present/dev/PACKAGING_RESULTS.md`.
