# ESP LV Present

[English](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README.md)

`esp_lv_present` 将 **LVGL 9** 接入已有的 [esp_display_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README_CN.md) Presenter，负责创建 LVGL display、flush、缓冲租约及帧提交。应用负责初始化面板、创建 Presenter，并运行 UI 任务。

## 接入要求与依赖

使用 ESP-IDF 6.0 及以上、LVGL 9.x，Presenter 的像素格式须与面板匹配。绑定支持 RGB565、RGB888，同一时间仅允许一个活动绑定。

使用这份源码时，在 `main/idf_component.yml` 中**同时指定两个本地组件**并替换路径，避免本地绑定解析到其他版本的 Present。仅从 Registry 接入须先发布兼容的组件版本。

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
  espressif/esp_lv_present:
    version: "^0.2.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_lv_present
```

LVGL 是绑定的公开依赖。LVGL 9.0–9.4 使用独立绘制缓冲并在 flush 时复制；LVGL 9.5+ 在 RGB565 布局兼容时可借用 Presenter 存储，其余布局走复制路径。借用有准入条件，不承诺所有模式均为零拷贝。FULL／DIRECT 的复制路径还需要 LVGL 分配器提供一整张逻辑帧的内存。复用 Presenter 时，先完成或取消上一帧并排空，再启动新绑定。

LVGL 9.5+ 的构建会生成 `lv_refr.c` 的刷新入口副本，依赖源码保持原样。
借用路径在布局更新、确认脏区后获取缓冲，然后才执行 DIRECT 同步和绘制。
无脏区的刷新发送 `LV_EVENT_REFR_READY`，不获取缓冲、不发送 `LV_EVENT_RENDER_READY`、不提交帧；
DIRECT 准入延迟则保留脏区并通过重试定时器继续。失效通知本身不等待缓冲。
生成器检查刷新入口结构；不兼容的 LVGL 更新会在配置时明确报错，需先复核集成点。
LVGL 9.0–9.4 的复制路径不需要此生成步骤。

## 启动 UI

先为初始化好的面板 [创建 Presenter](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README_CN.md#创建-presenter)，再在 UI 任务运行：

```c
#include "esp_lv_present.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// 向此任务传入已创建的 Presenter。
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

`start()` 会按需初始化 LVGL，并用 `esp_timer` 设置 tick 回调；每次 start 都会替换 LVGL tick 回调，不要另加 tick 来源。它**不创建**调用 `lv_timer_handler()` 的任务。start、UI 更新、timer handler 和 stop 应在同一 UI 任务串行执行。绑定运行期间，不要再并发调用 Presenter API 提交帧。

## 保留 UI 的运行时旋转

在启动绑定的 UI 任务中，与 `lv_timer_handler()` 串行、在 LVGL 回调之外调用：

```c
esp_err_t err = esp_lv_present_set_rotation(ESP_DISPLAY_PRESENT_ROTATE_90, 1000);
```

入口保留 display 和对象树，更新逻辑尺寸、Presenter 的 caps、角度、generation
和 `map_point()` 映射，并请求全屏重绘。不要再手动调用 `lv_display_set_buffers()`。
应用负责重置触摸状态（`lv_indev_reset()` / `lv_indev_wait_release()`）以及按新尺寸调整布局；
组件不修改面板 MADCTL。分辨率变化事件中的 Presenter 查询已反映新方向。

旋转前分配失败或排空超时保留旧方向，可以重试。旋转已发生、后续重绑失败时，
刷新暂停，Presenter 查询反映实际方向；应用应停止并重建绑定后恢复，不能假定失败等于回滚。
旧的公开 Presenter 旋转接口仍拒绝在官方绑定存活时直接调用。

## 停止与交接

在 UI 任务停止调用 LVGL，再调用 `esp_lv_present_stop()` 取消未完成帧、销毁 display 并解除绑定。成功后选择后续操作：

- **关闭：**调用 `esp_display_presenter_delete(presenter)`，删除成功后再释放应用持有的 panel／IO。
- **交给其他渲染器：**调用 `esp_display_presenter_quiesce(presenter, timeout_ms)` 排空再配置；再次启动绑定时需要重建 UI。

不要把 quiesce 成功作为停止绑定的前提：待刷新的 LVGL 可能已持有未完成帧。绑定存活期间也不允许修改 Presenter 旋转。

逐项检查返回值。无法完成时保留句柄，从当前阶段重试；取消或超时不代表硬件已释放像素。quiesce 返回 `ESP_ERR_INVALID_STATE` 时，用 `esp_display_presenter_get_state()` 检查状态；FAULTED 实例须安全删除并重建，不能无限重试复用。`stop()` 不删除 Presenter，成功后原 `lv_display_t *` 及其 UI 已失效。

交给不能自行重绑的 adapter 时，在排空后、启动对方前释放生产者所有权；参考 [交接示例](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff)。

`esp_lv_present_get_frame_count()` 统计提交的帧数，不是物理屏幕刷新次数。旋转与模式由 Presenter 管理，见 [模式、缓冲与任务组合](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README_CN.md#选择模式与流水线组合)。

## 参考

- [绑定公开 API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/include/esp_lv_present.h)。
- [LVGL benchmark 示例](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark)。
- [兼容与迁移](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md)。
- [联合 Host 回归](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md) 与 [目标端 Unity 测试](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/test_apps/README.md)。
