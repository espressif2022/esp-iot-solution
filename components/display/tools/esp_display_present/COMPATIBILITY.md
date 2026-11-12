# 兼容范围与迁移入口

当前组件由公开 Presenter、资源装配和唯一内部 engine 组成；LVGL 通过相邻
`esp_lv_present` 接入。不再要求应用引入独立 V2 组件。

- 主要 `esp_display_presenter_*` 调用入口保留；内部 `src/` 接口不承诺兼容。
- 原公开 geometry 头及部分类型已收为内部，依赖它们的应用需要迁移，不能宣称完整源码或 ABI 兼容。
- RGB/MIPI 按产品边界仅支持多 FB scanout，NONE 和单 FB 已删除；AUTO 按实际池选双/三缓冲 PARTIAL。
- Panel IO 保留 NONE 和 TE；TE 效果依赖真实面板时序，运行时旋转与存储限制仍须按当前合同检查。
- 超时和 cancel 不证明硬件停止；迟到完成只推进安全退休，不清除原故障。

当前行为以 [模式与缓冲合同](docs/MODE_ARCHITECTURE.md)、
[TE 时序](docs/TE_TIMING.md) 和 [生命周期规则](STATE_MACHINE.md) 为准。
构建/测试入口见 [开发说明](dev/README.md)，板测来源见
[最新五板记录](dev/VALIDATION.md)。

迁移阶段的 C1/C2、旧目录布局、旧默认模式和阶段性缺口保存在
[历史兼容记录 (archive)](dev/ARCHIVE.md)，不作为当前 API 清单。
源码迁移不代表 Registry 发布或全部外部应用兼容性已经验收。

## 2.0.0 候选的迁移边界

本次不是 1.x 兼容更新：公开 geometry 头/helper/type 移入内部，RGB/MIPI NONE
与单 FB 准入被删除，故声明为 2.0.0 候选；尚未发布 Registry 包。LVGL binding
对应 0.2.0，并依赖 present ^2.0.0。仓库内直接依赖的示例同步约束。

应用应只包含公开 `esp_display_present.h` / config / types；按当前公开区域和
配置结构更新调用，不以包含 `src/` 代替迁移。原先单 FB/NONE 的 scanout 应配置
至少两张驱动 FB 并使用支持的防撕裂模式；AUTO 不能再当作单 FB 兼容回退。
GRAM NONE 与 TE 的边界保留。新增 TRANSFER_FAILED 不能继续按 TIMEOUT 处理，
需要原始错误时使用 `esp_display_presenter_get_fault_error()`。

本表说明不承诺 ABI 兼容；旧外部应用应重新编译并验证，不能仅替换组件二进制。

## SDK capability and optional acceleration

The minimum supported SDK remains ESP-IDF 6.0. PPA and DMA2D executors also
require the SDK's private buffer-alignment API; DMA2D additionally requires the
async memcpy DMA2D header. CMake checks these APIs instead of assuming their
presence from SoC capabilities. Released IDF 6.0/6.1 lack these APIs: explicit
accelerator creation returns `ESP_ERR_NOT_SUPPORTED`, and resources use the
existing CPU executor fallback. No SDK patch is installed. Performance recorded
with an SDK providing the acceleration APIs must not be attributed to older SDKs.

DSI scanout additionally requires DPI `on_frame_buf_complete` and `on_vsync`
callbacks. IDF 6.0 does not provide them: DSI scanout creation returns
`ESP_ERR_NOT_SUPPORTED`. Its old `on_refresh_done` is not substituted for buffer
retirement. Thus a successful 6.0 P4 compile validates core/CPU integration, not
DSI scanout availability. The P4 DSI measurements use the newer SDK recorded with
their evidence. RGB and IO support remain subject to their documented driver
capabilities, independently of this DPI requirement.
