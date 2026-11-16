# ESP LV Present

[中文](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md)

`esp_lv_present` connects **LVGL 9** to an existing
[esp_display_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README.md) presenter. It creates the
LVGL display and handles flush, buffer leases and frame submission. Your
application initializes the panel, creates the presenter and runs the UI task.

## Requirements and dependency setup

Use ESP-IDF 6.0 or newer, LVGL 9.x, and a presenter whose pixel format matches the
panel. The binding supports RGB565 and RGB888 and one active binding at a time.

For this source checkout, select **both** local components in
`main/idf_component.yml`; replace the paths below. This avoids resolving a local
binding against a different Present version. Registry-only use requires the
compatible component versions to be published first.

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
  espressif/esp_lv_present:
    version: "^0.2.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_lv_present
```

LVGL is a public dependency of the binding. On LVGL 9.0–9.4, it uses a separate
draw buffer and copies on flush. On LVGL 9.5+, compatible RGB565 layouts can
borrow presenter storage; other layouts use the copy path. Borrowing is
conditional, not a universal zero-copy guarantee. FULL/DIRECT on the copy path
need a full logical frame from the LVGL allocator. When reusing a presenter,
finish or cancel the previous frame and quiesce it before starting a new binding.

## Start the UI

First [create a presenter](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README.md#create-a-presenter)
for the initialized panel, then run the following on the UI task:

```c
#include "esp_lv_present.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pass an existing presenter to this task.
void ui_task(void *arg)
{
    esp_display_presenter_t *presenter = arg;
    lv_display_t *display = NULL;
    ESP_ERROR_CHECK(esp_lv_present_start(presenter, &display));

    lv_obj_t *label = lv_label_create(lv_display_get_screen_active(display));
    lv_label_set_text(label, "Hello, Present!");
    lv_obj_center(label);

    for (;;) {
        lv_timer_handler();
        vTaskDelay(1);
    }
}
```

`start()` initializes LVGL if needed and sets its tick callback from `esp_timer`.
Each start replaces the LVGL tick callback; do not add another tick source.
It does **not** create a task to call `lv_timer_handler()`. Start, UI updates,
timer handling and stop must be serialized on the same UI task. Do not submit
frames through the Presenter API concurrently with this binding.

## Stop or hand off

On the UI task, stop calling LVGL, then call `esp_lv_present_stop()` to cancel
any unfinished frame, destroy the display and detach the binding. After it
succeeds, choose the next operation:

- **Close:** call `esp_display_presenter_delete(presenter)`; release the
  application-owned panel/IO only after deletion succeeds.
- **Reuse or rotate:** call `esp_display_presenter_quiesce(presenter, timeout_ms)`
  before reconfiguring. Restart the binding and recreate the UI after rotation.

Do not require quiesce to succeed before stopping the binding: a pending LVGL
refresh may already hold an unfinished frame. Presenter rotation is rejected
while the binding is active.

Check each return value. If an operation cannot complete, retain the handles
and retry from that stage; cancellation or timeout does not release pixels still
owned by hardware. If quiesce reports `ESP_ERR_INVALID_STATE`, check
`esp_display_presenter_get_state()`: a FAULTED presenter must be safely deleted
and recreated, not retried indefinitely for reuse. `stop()` does not delete the
presenter; after it succeeds, the old `lv_display_t *` and its UI are invalid.

To hand off to an adapter that does not rebind itself, release producer ownership
after quiesce and before starting it. Follow the
[handoff example](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff).

`esp_lv_present_get_frame_count()` counts committed frames, not physical panel
refreshes. Rotation and mode selection belong to the presenter; see the
[Present mode, buffer and task combinations](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README.md#choose-a-mode-and-pipeline-combination).

## Reference

- [Public binding API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/include/esp_lv_present.h).
- [LVGL benchmark example](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark).
- [Compatibility and migration](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md).
- [Joint Host regressions](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md) and [target Unity tests](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/test_apps/README.md).
