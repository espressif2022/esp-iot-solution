# C2 第二轮：旧契约与逐笔传输期限（2026-11-02）

本轮不烧录、不切换示例默认依赖、不提交 git commit。修正上一切片门面的行为差异，不改变 GRAM/TE/FB 的缓冲拓扑、所有权和退休机制。

| 验收 | 结果 |
| --- | --- |
| Host + 实际 LVGL | 34/34 测试条目 |
| ASan / UBSan / LSan | 34/34，`ASAN_OPTIONS=detect_leaks=1` |
| 重复回归 | legacy_resources、legacy_facade、transfer_timeout 各连续 50 次，共 150 次 |
| 格式 / 补丁 | `dev.py format --check` 与 `git diff --check` 通过；test_fb.c 只拆分两处同一行多语句 |
| 头文件 / 符号 | 21 个 V1 导出；三份 presenter 头文件及 LVGL 头文件仍与原件逐字节一致 |
| S3 / P4 组件离线编译 | 通过；SDK 副本中五份相关生产源码/头文件与当前工作区一致 |
| P4 三原示例离线重新链接 | benchmark、GSP/LVGL handoff、showcase 均通过；业务 C/C++ 不变 |

showcase 仍使用缓存 GSP 1.9.0，原 manifest 的 `1.5.*` 发布组合没有验收。离线链接不证明 GSP 实际运行、屏幕无撕裂或获得某个 FPS；本轮没有新的实板轨迹和性能数据。

## 修正的 V1 行为

- dirty 指针/count/容量/上限和坐标必须先校验；首帧全绘不能掩盖非法输入。V1 拒绝原始越界 dirty，只把合法区域对齐扩展后裁到屏幕边界。
- 全绘返回 `full=true, count=0`，支持 NULL request 与无输出数组的合法全绘；不将该标志当成实际覆盖证据。
- PARTITION 拒绝 scroll；FULL/DIRECT 校验 scroll viewport 和非零位移后要求完整 raster。没有伪造滚动拷贝。
- buffer.resolve_rows 保存本帧 producer 的高度对齐；拆 band 时向下对齐，零行和超过 UINT16_MAX 的结果拒绝；完整请求的最后 band 不额外缩短。
- GRAM 的 completed_present 是旧接口的所有权水位，随成功 transfer 推进；VSYNC 额外确认仍只由 confirmed_present 表示。
- 已到达的 tracking 确认先发布，再判断取消是否过期；generation=0 拒绝。handoff 先观察底层 fault，不清故障；故障后的 quiesce 不再按成功水位空等。
- 非 TE GRAM 与 DIRECT 沿用 V1 的非零旋转限制。PARTITION/FULL 的四方向初始旋转在 8×4 非方形画布逐像素对照。V1 固定逻辑形状，而原生 V2 固定物理形状；门面拒绝无法保持两者契约的非方形运行时跨轴切换，不静默交换逻辑宽高。原生 V2 旋转能力不变。
- LVGL 已暂停 raster 但绑定仍存在时允许 quiesce 排空；有 active frame/cancel 时仍拒绝。延迟完成的 Host 用例验证第一次超时不拆绑定，真实完成后重试成功，再执行 LVGL stop。
- AUTO 的 TE 选择使用 te_enabled 且 GPIO>=0；GPIO0 有效，GPIO=-1 禁用。明确 NONE 不偷换成 TE，真正 TE 请求仍因平台模型未闭环而明确拒绝。

Host 直接编译 V1 的纯 present_geometry.c 作为参考，700 组合法区域/非幂次对齐/边界裁切做差分；4 方向×PARTITION/FULL 对照物理像素与正反坐标。该 V1 源码仅用于 Host 测试，不进入 V2 生产依赖。

## 逐笔传输期限

在现有 Presenter 的 panel.submit/event 入口统一实现，不向三个后端分别加超时状态机。计时记录仅保存 cookie、开始时刻和已报告标志，不持有像素引用，也不改变 lease、pending 或扫描源状态。

原生 `pv2_presenter_config_t.transfer_timeout_ms=0` 默认关闭；旧门面把 V1 的 0 按原约定映射为 1000 ms。计时表仅在启用时分配，分配量纳入计划预算。期限从真正调用底层 panel.submit 起算，包含驱动排队和完成通知延迟；绘制、拷贝、repair、等待 TE 不算物理传输时间。明确拒收撤回计时，不置 sticky fault；不确定接受保留计时和所有真实源引用。

超过期限只发布 FAULT。迟到完成入口先报告期限故障，再转交原始 DONE/SCANOUT_CONFIRMED，使后端既保留失败结果，又消费真实退休证据；不伪造完成。原 sender 在最近期限唤醒，没有新增线程。同步 GRAM 没有 sender 时依靠 service/状态/等待及完成入口检查，不另起空闲定时器。不可中断一个永久阻塞的底层驱动调用，也不承诺硬实时报警。

新测试覆盖：未封帧 push、多个 push 独立起点、多实例隔离、拒收重试、早到完成、无 task 查询时的迟到完成、不确定接受、stop 超时保持可读 DMA 源、晚到 scanout 确认、等待 TE 不误触发传输期限、既有 sender 无 owner 查询也能报告超时，以及新增元数据分配失败回滚。异步故障测试改用条件变量固定“已封帧后失败”，消除旧测试对调度快慢的依赖。

## 仍未关闭的门槛

- TE 门面：需同时闭环时序模型/上界来源、SPI 源内存 DMA 能力和 GPIO 事件源的停止/加入/失败回滚；bus_freq/data_lines 只有估计，不能据此伪造已验证窗口。核心 TE 的 Host 期限测试不等于 GPIO/屏幕验收。
- 单 FB、未确认 bounce 的 RGB、FB swap、SPI 不明 direct-PSRAM DMA 等仍明确拒绝。单 FB 还需驱动事件语义和 active-scanout-writes 策略的专项验收。
- 非方形运行时跨轴在旧 API 下仍未支持，需先确定扩展契约。
- C3 三板默认配置/真实 GSP/benchmark，C4 常驻 coverage 位图收敛，C5 条件性调度优化/默认切换均未完成。未把 C2 标记整体完成。

日志、源码/头文件/二进制 SHA256 在本地 `artifacts/20261102-contracts/summary.json` 及相邻日志，git 忽略。Host 不证明竞态穷举、真实 IRQ/缓存/驱动时序或光学无撕裂。
