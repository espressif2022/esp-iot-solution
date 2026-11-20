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
- **生产者任务（1.2.2 首次帧认领 → 2.0 创建即绑定）**：1.2.2 允许创建后由首个调用 `begin_next_frame()` 的任务自动认领 producer；2.0 在 `create()` / `open()` 时即把**调用任务绑定为 producer**。若应用在主任务创建 presenter、在独立渲染 worker 线程提交帧，必须在确认原任务停止提交后由新任务在帧间显式调用 `esp_display_presenter_rebind_producer()`（或由原任务先调用 `esp_display_presenter_release_producer()`），不能再直接沿用 1.2.2 的首次帧自动认领假设。
- **`commit_frame()` 完成语义（1.2.2 GRAM 等空闲 → 2.0 封帧即异步提交）**：1.2.2 的 GRAM 路径在 `commit_frame()` 内等待传输空闲才返回；2.0 的 `commit_frame()` 返回 `ESP_OK` 仅代表当前帧已封帧并交由 backend 调度，最后的 Panel IO / scanout 传输允许继续异步在途。不能再把 `commit_frame()` 成功当作物理传输或屏幕显示结束：需要明确完成保证的位置（如截图、完成统计、切换或销毁前）须显式调用 `esp_display_presenter_quiesce()`；正常连续渲染循环则建议依靠下一帧 `acquire_buffer()` 的自然背压来保留异步流水线重叠收益，避免无条件在每帧末尾 `quiesce()`。
- **失败事务恢复（1.2.2 自动 cancel/reset → 2.0 保留活动帧现场）**：1.2.2 在 `submit_buffer()` 或 `commit_frame()` 失败时会自动取消并重置当前活动帧；2.0 在 `begin_next_frame()` 成功之后，若 `acquire_buffer()`、`submit_buffer()` 或 `commit_frame()` 返回失败，**活动帧（及尚未消费的 lease）仍保持开启**，不会自动清理。下游必须区分两类错误恢复：
  - `begin_next_frame()` 成功且 `commit_frame()` 尚未成功（`acquire` / `submit` / `commit` 失败）：由当前 producer 任务显式调用 `esp_display_presenter_cancel_frame()` 清理活动帧后，再开始下一帧；
  - `commit_frame()` 已返回成功、后续 `quiesce()` 等等待超时或失败：帧已封帧提交，`cancel_frame()` 不会撤销已进入硬件的传输，也不构成释放缓冲区的依据；应保留仍在使用的资源，按 `STATE_MACHINE.md` 重试 `quiesce()`，或按 fault 状态安全 `stop()` / `delete()`。
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

DSI 扫描输出优先使用 ESP-IDF 6.1+ 提供的 DPI `on_frame_buf_complete` 与 `on_vsync` 回调。在仅提供 `on_refresh_done` 的 ESP-IDF 6.0 上，适配器按芯片最小版本配置区分事件语义：`CONFIG_ESP_REV_MIN_FULL < 300` 时 `on_refresh_done` 在 DW-GDMA 切换链表后立即触发，采用单边界退休；`CONFIG_ESP_REV_MIN_FULL >= 300` 时 `on_refresh_done` 在 DSI Bridge VSYNC 触发（晚于 DW-GDMA 链表重载中断），多缓冲切换会额外等待一个刷新周期以确保旧帧缓冲真正退出 DMA 读取，单缓冲曝光确认不变。（注意：在精确的 ESP-IDF `v6.0.0` 标签上，移交 `esp_display_present` 管理的 DPI 面板请勿额外调用 `esp_lcd_dpi_panel_enable_dma2d()`，否则驱动旧分支顺序会拦截零拷贝切帧；`v6.0.1+` 已修复。）
