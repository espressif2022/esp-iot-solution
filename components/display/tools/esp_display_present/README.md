# ESP Display Present

`esp_display_present` provides the stable `esp_display_presenter_*` API, platform resource assembly, and one presentation engine. `esp_lv_present` provides the LVGL binding to that engine. Existing applications keep their presenter API and own the panel, IO, and bus resources.

Public headers are in `include`; internal engine headers in `src` are not application API.

For local component discovery add `esp_display_present` and, for LVGL, `esp_lv_present`; neither requires a V2 component. Registry publication and external compatibility have not been established by this source migration. See [current development and validation](MIGRATION_STATUS.md) and the [historical transition design](../esp_display_present_v2/TRANSITION_DESIGN.md).

See [TE timing and scheduling](docs/TE_TIMING.md) for automatic measurement, the shared window model, and compatibility boundaries.
