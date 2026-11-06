# Present 架构检查与 Host 收敛 · 2026-11-06

本轮从创建、租用、提交、完成、取消到销毁检查公开 presenter、资源装配、engine 和 backend。DMA/PPA 专项仍按 [后续项](DMA_PPA_FOLLOWUPS_20261106.md) 暂缓；没有调整其实现，也没有合并任务或更改缓冲数量策略。

## 状态与资源的权威

| 对象 | 唯一负责的事实 | 不应据此推断 |
|---|---|---|
| 应用拥有的 panel/IO、平台 adapter | 固定物理尺寸、driver FB 地址、硬件事件和回调注册 | 逻辑旋转不能改 panel 物理宽高 |
| backend、allocation/image/frame core | 真实像素占用、图像版本、帧记录、读取何时结束 | 收到一帧完成不等于当前 scanout FB 可写 |
| engine | 生产者所有者、调度、已提交回执队列、背压和停止 | 超时不能合成 DMA 完成或释放像素 |
| 公开 presenter | 应用会话、lease ID 映射、generation、旧 API 状态/显示跟踪 | 不独立推导底层 buffer 是否可复用 |
| resources / plan | 装配和创建时分配规划 | plan 不是运行时布局或当前内存占用快照 |
| LVGL binding | 将绘制/flush 接入 lease 生命周期 | LVGL flush_ready 不直接证明 DMA/scanout 完成 |

多层 active/cancel/receipt 并非全部重复：公开会话撤销、engine 的异步取消 drain、backend 的硬件读取结束处于不同阶段。不能仅因字段同名就合并。现有生产者串行化、控制调用的暂停前提和 delete 独占约束保留。

## 已复现并修复：逻辑尺寸与物理尺寸存在两个修改入口

旧资源策略 fixed_rotation=true，让非方形屏幕跨轴旋转后仍保留旧逻辑宽高；资源层随后又调用 scanout_set_layout 交换物理视图。真实 panel 的物理宽高没有改变，后续提交会与驱动窗口不一致。旧 Host 用例在旋转后也修改了 fake panel 物理尺寸，掩盖了这一边界错误。

先固定 mock 的物理尺寸后，旧实现的 0→90° 在逻辑 caps 断言失败：8×4 未变成 4×8。失败日志保留在本轮证据中；历史 P4 板测越界现象与此根因一致，但本轮没有重新上板。

本轮修复：公开资源装配使用 backend 已有的逻辑轴交换；删除资源层对 scanout 的第二次尺寸提交，并删除私有物理布局修改接口。panel/driver FB 保持不变，engine/backend 更新逻辑 caps，公开 presenter 从 caps 更新逻辑配置。旋转后的生产者必须重新查询 caps，不能继续使用旧 stride/区域。

## Host 验证

- 基线 37/37；修复及扩充用例后 ASan/UBSan/LeakSanitizer 全套 37/37，detect_leaks=1。
- 6 种公开创建配置：同步内部 GRAM、PSRAM staging GRAM、旋转 GRAM、RGB FULL、DSI DIRECT、双 compose TE。对跨模块 metadata/pixel 分配及 mutex/event/worker 创建逐点注入，140 个失败点均可回滚；失败后同一 panel 能重新创建/销毁，创建过程不发送图像、不删除应用的 panel。
- 公开 delete 在 engine 已释放、执行器清理失败后保留对象，可查询 STOPPED 状态并重试 stop/delete。
- RGB/DSI × 7 个 FB 模式 × 4 个初始方向，每例进行初始帧和两次跨轴后帧，共 168 帧；mock 物理尺寸固定，核对输出窗口、逻辑尺寸、generation 和像素。原测试中的跟随旋转修改物理屏幕逻辑已移除。
- GRAM 内部/PSRAM × 4 个初始方向 × 连续 4 个方向，共 32 帧；固定物理屏幕，用独立坐标公式核对像素，不调用生产 geometry 作为预期值来源。
- FULL/DIRECT 从物理 FB 切到逻辑画布时注入分配失败，确认尺寸和 generation 不变，随后重试成功。
- 现有迟到/提前完成、明确拒收/不确定接受、取消 pending copy/transfer、receipt 释放、超时和控制线程检查仍全部通过。
- S3 + LVGL、P4 smoke 实际 ESP-IDF 编译通过；没有烧录，IDF 实现未修改。

故障注入针对上述框架入口，不是对 libc/pthread/驱动内部所有分配点的穷举。Host 保证的是模型事件下的所有权与状态行为，不是对所有并发交错的形式化证明。

## 仍保留的设计边界

- 每个 engine 一个串行生产者，handoff 需要旧生产者停止调用；公开 LVGL start/stop 仍是单实例。
- copy worker 与 sender 分工保留；队列容量、buffer_count 和任务优先级会限制重叠程度。draw 与 TX 的生命周期独立，但数量仍绑定。
- plan 是创建期规划；运行时 FULL/DIRECT 可能为旋转增加/释放逻辑画布。后续若需要实时内存预算或完整当前拓扑诊断，应增加单独的运行时快照，不能直接把旧 plan 当作当前事实。
- 运行时原地 swap 的 GRAM 无转换目标时仍可拒绝旋转；不为扩展能力偷偷更换资源策略。
- 真实 cache/DMA、跨核 ISR、TE 时间窗和光学效果须上板；本轮修复需 P4 非方形屏幕复测。

相关代码主要是 src/present_resources.c、src/platform/present_lcd_scanout.c、src/esp_display_presenter.c；测试在 dev/tests/test_legacy_facade.c。分层检查没有发现需要重写整个 engine 的依据，本轮只收敛有反例的尺寸权威冲突并补齐生命周期验证。
