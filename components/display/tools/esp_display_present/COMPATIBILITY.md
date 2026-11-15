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

## LVGL 与 GSP

`esp_lv_present` 只编译依赖 Present 的私有 renderer 接口。LVGL 9.5 起，
符合像素格式与布局条件时可以借用缓冲；较早的 LVGL 9 通过独立绘制缓冲
完成 flush。Host 实际运行过的版本见 [验证记录](dev/VALIDATION.md#host-gsp--lvgl-handoff)。

共享 Presenter 时，若下一渲染任务的 adapter 不提供显式重绑，由当前所有者在
帧间调用 `esp_display_presenter_release_producer()`，再启动下一渲染任务。
首次成功开始的帧取得任务所有权。GSP 暂停后，LVGL 可以用
`esp_lv_present_start()` 接手；LVGL quiesce 并 stop 后再释放所有权，
随后使用同一个 pause token 恢复 GSP，成功后丢弃 token。

## SDK capability and optional acceleration

The minimum supported SDK remains ESP-IDF 6.0. CMake detects the available
DMA2D APIs independently of the SoC capabilities; a missing private alignment
helper no longer disables both accelerators. No SDK patch is installed.

| Available SDK capability | Present executor path |
|---|---|
| DMA2D SoC and `esp_async_color_convert.h` | Public asynchronous color-convert API, with equal source/destination formats for 2D copies |
| DMA2D SoC and legacy `esp_private/async_memcpy_dma2d.h` only | Legacy asynchronous 2D memcpy API |
| PPA SoC and the PPA driver | PPA SRM for copy, rotation and supported byte-order conversion |
| Missing accelerator/API, or a request rejected by its probe | CPU fallback before hardware execution |

When available, `dma2d_get_buffer_alignment_constraint()` supplies the memory
constraints. Without it, the executor admits ordinary internal RAM and
unencrypted, non-ECC PSRAM; unsupported external-memory constraints are rejected
before submission. Cache-line, stride, capacity and transform checks still apply.
The public DMA2D driver synchronizes the declared destination picture, so its
picture window is restricted to the affected rows where alignment permits it.
Entering hardware execution never authorizes CPU fallback on an error or a
missing callback: its buffers must remain borrowed until completion is proven.

The facade creates accelerator clients only when its mode/rotation needs pixel
copies, with one pending hardware transaction per client. Eligible FB paths
reuse the existing backend worker for asynchronous copies outside the engine
gate, avoiding an additional pixel-worker stack. Advanced configurations retain
the dedicated pixel worker. Neither choice changes the pixel-buffer count or
the requirement to retain in-flight storage on a stop timeout. SDK revisions,
actual hardware execution and measured memory costs are recorded with the
[validation results](dev/VALIDATION.md); successful compilation alone is not
evidence that an accelerator ran.

DSI scanout additionally requires DPI `on_frame_buf_complete` and `on_vsync`
callbacks. IDF 6.0 does not provide them: DSI scanout creation returns
`ESP_ERR_NOT_SUPPORTED`. Its old `on_refresh_done` is not substituted for buffer
retirement. Thus a successful 6.0 P4 compile validates core/CPU integration, not
DSI scanout availability. The P4 DSI measurements use the newer SDK recorded with
their evidence. RGB and IO support remain subject to their documented driver
capabilities, independently of this DPI requirement.

P4 can use DPI's own cache writeback for a registered whole-frame scanout
buffer. This requires the audited DPI, draw wrapper and cache sources, and the
private BSP binding must report the live panel's native draw path, physical
dimensions and input bits per pixel. Otherwise Present performs its own
writeback before submit. The P4 EK79007 test BSP supplies the binding from its
DPI initialization. [BSP contract](src/platform/lcd/present_lcd_dpi_info.h)
