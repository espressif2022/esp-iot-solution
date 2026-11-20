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
构建与测试入口见 [Host 回归说明](tests/host/README.md)。

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

Panel IO NONE 根据最终分配的输入缓冲数量决定提交上下文：一块由调用任务处理，
两块由单个 backend 任务处理；自动两块降为一块时同步收回 worker 预算。
下游应验证缓冲等待、任务交接和实际传输耗时，不能仅凭接口同名认定调度等价。

### 仅针对 2.0 开发中间版本

中间版本曾提供 `target.io_submit` 和 `esp_display_present_io_submit_t`，最终版本已删除；
使用该中间版本的初始化项需要移除 `.io_submit`，并按上述缓冲数量规则配置。
正式 1.2.2 没有这个字段，因此这不是 1.2.2 用户的必做迁移步骤。
旧中间版 D2 CALLER / D1 BACKGROUND 的独立组合不再保留。

本表说明不承诺 ABI 兼容；旧外部应用应重新编译并验证，不能仅替换组件二进制。

## LVGL 与 GSP

`esp_lv_present` 只编译依赖 Present 的私有 renderer 接口。LVGL 9.5 起，
符合像素格式与布局条件时可以借用缓冲；较早的 LVGL 9 通过独立绘制缓冲
完成 flush。Host 及可选 GSP 集成测试的依赖见 [回归说明](tests/host/README.md)。

共享 Presenter 时，若下一渲染任务的 adapter 不提供显式重绑，由当前所有者在
帧间调用 `esp_display_presenter_release_producer()`，再启动下一渲染任务。
首次成功开始的帧取得任务所有权。GSP 暂停后，LVGL 可以用
`esp_lv_present_start()` 接手；LVGL quiesce 并 stop 后再释放所有权，
随后使用同一个 pause token 恢复 GSP，成功后丢弃 token。

## 正式 1.2.2 到 2.0 的下游迁移清单

- **依赖组合**：Present 2.0 配套官方 `esp_lv_present` 0.2.0；更新 manifest 后重新生成
  锁文件。旧的固定 1.x / `^1.x` 项目不会因发布 2.0 自动升级。移除联调 override，
  在干净环境验证实际发布包；Vibe 等子模块项目先提交子模块再更新父仓库 gitlink。
- **生产者任务**：创建时认领调用任务；另一绘制任务接管时显式 `rebind_producer()`，
  或由当前所有者在帧间 `release_producer()` 后交接。无需把每帧重绑作为通用要求。
- **区域类型**：旧 `esp_display_presenter_region_t` 可迁为应用自己的 surface + origin
  描述；缓冲写入仍使用 acquire/resolve_rows/submit/commit，不包含组件内部头文件。
- **官方 LVGL 运行时旋转**：使用 `esp_lv_present_set_rotation(angle, timeout_ms)`，
  保留 UI 并同步 Presenter 查询。原先旋转后手动获取池地址、重绑 LVGL 缓冲的流程不再适用。
  触摸复位由应用负责，晚期失败按实际方向处理。Claw 自带 `esp_lvgl_present` 是另一套绑定，
  不应直接套用官方绑定迁移方式。
- **硬件与故障**：复核至少两张 scanout FB、DSI 回调能力、SPI PSRAM DMA 配置与 BSP
  一致性，以及 TRANSFER_FAILED、超时、迟到完成和停止时的资源保留。

### GSP 与下游发布的遗留验收

这些事项独立于公共旋转入口，不能以 Host 通过代替产品发布验收：

- GSP 旧 archive 使用的公开配置 ABI 与 2.0 不同；须取得匹配产品接口的源码、重新编译
  并发布匹配包。仅重编应用或放宽版本约束无效。
- Claw 还需重新审查输入/背景图产品补丁和 archive 装配检查；Vibe 的旧 GSP 模板单独迁移。
  源码 GSP 测试不证明 Registry 旧预编译包兼容。
- GSP 旧 geometry/rotate/PPA 微基准需要迁移或明确退役；跳过它们不等于完成验证。
- `--wrap=esp_display_presenter_submit_buffer` 看不到官方 LVGL 的内部 Engine 提交。
  截图/镜像须按 GSP、LVGL、自绘及切换分别验证，不能据此认定所有现有镜像都失效。
- Webcam 使用官方旋转入口可移除应用额外整帧桥接；绑定仍有自身存储，内存峰值、
  吞吐、TE 时序和动态旋转须在最终产品固件上验证。

## SDK 能力检测与可选硬件加速

最低支持的 SDK 仍为 ESP-IDF 6.0。CMake 会独立于芯片能力检测可用的 DMA2D 接口；缺少私有对齐查询辅助函数时，不再同时禁用两个硬件加速器。组件不依赖任何 SDK 补丁。

| 可用的 SDK 能力 | Present 执行路径 |
|---|---|
| DMA2D SoC 且存在 `esp_async_color_convert.h` | 公开异步颜色转换接口，源与目标格式相同时用于 2D 拷贝 |
| DMA2D SoC 且仅存在旧版 `esp_private/async_memcpy_dma2d.h` | 旧版异步 2D memcpy 接口 |
| PPA SoC 与 PPA 驱动 | 使用 PPA SRM 完成拷贝、旋转及支持的字节序转换 |
| 缺少加速器／接口，或请求被能力检查拒绝 | 执行前退回 CPU 实现 |

存在 `dma2d_get_buffer_alignment_constraint()` 时由其提供内存对齐与域约束；缺少该函数时，执行器允许普通内部 RAM 及未开启加密和 ECC 的 PSRAM，不支持的外部内存约束会在提交前被拒绝，同时仍执行 cache line、stride、容量和变换检查。公开 DMA2D 驱动会同步声明的目标图像区域，因此在对齐允许时将图像窗口限制在受影响行范围内。一旦进入硬件执行，错误或缺失回调时不会退回 CPU，相关缓冲必须保持借用直到确认硬件完成。

门面层仅在当前模式／旋转确实需要像素拷贝时创建加速器客户端，每个客户端同时只允许一个在途硬件事务。符合条件的 FB 路径复用现有 backend 工作线程在 engine gate 之外执行异步拷贝，避免额外分配 pixel 工作栈；高级配置仍保留独立的 pixel 工作线程。两种方式都不会改变像素缓冲数量，也不会免除停止超时时保留在途存储的要求。硬件能力验收应记录实际使用的 SDK 版本与有效配置，编译通过本身不代表加速器在运行时已启用。

DSI 扫描输出额外依赖 DPI 的 `on_frame_buf_complete` 与 `on_vsync` 回调。ESP-IDF 6.0 不提供这两个回调，因此在该版本上创建 DSI scanout 会返回 `ESP_ERR_NOT_SUPPORTED`；组件不会用旧的 `on_refresh_done` 替代缓冲退休语义。基于 IDF 6.0 的 P4 编译通过仅验证核心层与 CPU 路径集成，不代表 DSI 扫描输出可用；P4 DSI 实测需配合提供上述 DPI 回调的较新 SDK。RGB 与 Panel IO 的支持范围仍各自取决于对应驱动能力，不受此 DPI 限制影响。
