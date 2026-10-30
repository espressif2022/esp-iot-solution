# 显示提交与资源生命周期

本设计将“函数返回”“硬件完成”“buffer 可复用”分别定义。等待超时只结束本次等待，不自动取消硬件访问，也不归还 buffer。

当前 GRAM／TE／FB、异步 pixel FIFO、非阻塞 repair、Presenter、LVGL 复制与受限 FULL 零拷贝均已实现。DSI／RGB bounce 与 DMA2D／PPA 桥接已通过 Host 驱动 stub 和 P4 离线编译；真实硬件时序与 cache 行为未验收。支持矩阵以 README 与 PLAN 为准。

## 完成与释放

| 事件 | 保证 | 不保证 |
| --- | --- | --- |
| 像素任务终结 | 拷贝引擎不再访问 src/dst，必要 cache 操作结束 | 失败时目标像素正确 |
| GRAM 传输完成 | 该次发送不再读取源/发送 buffer | 屏幕已扫描出所有像素 |
| FB 显示确认 | 新 buffer 已按平台定义成为显示源 | 新 buffer 可以复用 |
| FB 退役 | 旧 buffer 不再被扫描或硬件引用 | 所有其他 frame 同时完成 |

完成 receipt 记录事件与结果，不代替 buffer 所有权。当前 scanout buffer 在没有后续帧时可能持续被引用；receipt 已完成或被用户释放，也不能释放它。

## 通用规则

1. **构建与获取 (`begin` / `acquire`)**
   - `begin` 创建一帧构建上下文并预留完成记录，资源不足时在发送任何像素前拒绝（`ESP_ERR_NO_MEM`）。
   - `acquire` 返回独占写 lease：
     - 传入非空 `area` 时，后端按 tile 容量裁剪高度并返回实际 `logical_area` 与紧凑 `stride_bytes`。
     - 传入 `area == NULL` 时（对接 `acquire_buffer` 无预选区域模式），返回默认满宽 scratch/surface lease，调用方随后在 `submit_region_ex` 中提供实际绘制的 `logical_area` 与 `stride_bytes`。`logical_area.x/y` 是屏幕目的坐标，源像素从 buffer 起点读取，不是按该坐标裁剪大图。
   - `get_draw_buffers` 允许在 PARTITION 模式下借出底层绘制池指针供渲染器（如 LVGL）静态注册，这些地址本身不授予写入权；必须在绘制前持有对应 lease，每次刷屏仍须遵循 `begin → acquire → submit_region(_ex) → end` 生命周期。

2. **区域提交与回滚 (`submit_region` / `submit_region_ex` / `cancel_region`)**
   - `submit_region` / `submit_region_ex` 返回 `ESP_OK` 消费 lease；调用方不得继续读写，即使 CPU 已同步完成也必须重新 `acquire`。
   - 返回非 `ESP_OK` 时不消费 lease（保证未启动任何硬件访问），若期间执行了原地字节序转换则自动回滚至调用方字节序，由调用方重试、`cancel_region`，或通过 `cancel_frame` 统一回收。
   - 任何已经启动或不确定是否启动的硬件访问（`PV2_TX_ACCEPTED` / `PV2_TX_UNCERTAIN` 或驱动返回前已触发早到回调）都必须返回 `ESP_OK`（已消费 lease）并隔离资源，之后通过帧状态/receipt 及后端 `get_fault` 报告故障；绝不在硬件仍可能访问内存时返回可重试的普通拒绝。
   - 帧内任一已接受区域发生异步故障（`PV2_EVENT_FAULT` 或部分接受报错）后，`f->result` 置为错误码；该帧后续新的 `acquire` / `submit_region` 将立即拒绝（`ESP_ERR_INVALID_STATE`），调用方应调用 `cancel_frame` 或 `end` 收尾。

3. **帧封闭与 Receipt 生命周期 (`end` / `end_ex` / `release_receipt`)**
   - `end` / `end_ex` 要求当前无未提交的活跃 lease。成功表示帧已封闭，禁止再写入；不承诺此时像素已到达面板。
   - **双模式完成记录回收**：
     - **显式追踪 (`out_receipt != NULL`)**：发布外部 receipt（绑定实例与单调 generation token）。调用方必须在 receipt 到达 `terminal` 后调用 `release_receipt` 释放记录槽位。
     - **Fire-and-forget (`out_receipt == NULL`)**：不发布外部 receipt。若封闭时所有区域已完成则立即回收记录；否则在最后一个 in-flight 区域完成回调（ISR/task）到达时由后端自动回收记录槽位，避免无追踪诉求的调用方耗尽 `max_frames`。首次已接受操作的错误另外保存到后端 sticky fault（frame token、transaction cookie、错误码），由 `get_fault` 查询，直到后端销毁；后续成功帧不会清除。
   - `end_ex` 支持在封闭帧时显式提交 dirty coverage 区域列表（供 TE / FB 的 `FULL` / `DIRECT` 模式执行前帧 repair）。

4. **帧取消与孤儿帧自愈 (`cancel_frame`)**
   - `cancel_frame` 停止接收新区域，立即归还所有尚未提交的 `SLOT_LEASED` 缓冲；已接受的 copy/传输继续排空。 GRAM 已发出的区域无法撤销。
   - 若等待排空超时返回 `ESP_ERR_TIMEOUT`：帧保持 `sealed + canceled` 状态及所有 in-flight 硬件资源。
   - **异步自愈与幂等重试**：当已取消帧的最后一个 in-flight 传输在后台 ISR 中完成时，后端自动回收该帧记录并清除 `active` 占用，使后续 `begin` 不被永久卡死；同步或异步完成取消时均记录最近一次取消的 token；在下一次取消完成前，该 token 可重复重试任意次且返回 `ESP_OK`。普通 begin/end 不消费这条历史；更旧、跨实例或未知 token 返回 `ESP_ERR_INVALID_STATE`。已 end 的帧不可改走取消路径。

## GRAM 分块发送

```text
FREE → LEASED → [CONVERTING / PACKING] → SUBMITTING → INFLIGHT → FREE
                    |                         |
                 转换失败               驱动拒绝且无早到回调
                    |                         ↓
                    └───────────────→ (回滚原地转换) 保留 lease，返回失败
```

转换与紧凑化（Packing）：
- Panel Adapter 的 GRAM `submit` 要求源 surface 紧凑连续（`stride_bytes == width * bpp`）。
- 若调用方通过 `submit_region_ex` 传入了带 padding 的 `stride_bytes`，或者 `draw_order != panel_order` 且未开启 `in_place_convert`，后端通过像素引擎转换/紧凑化至独立 `converted` DMA 缓冲，保持到 DMA 完成。
- 若开启 `in_place_convert` 且 stride 紧凑，像素引擎在租约移交后直接对源 tile 原地翻转字节序（节省一半内部 DMA SRAM）；若随后驱动返回 `PV2_TX_REJECTED`（且无早到回调），后端在提交函数返回前将字节序翻转回 `draw_order`，保证调用方重试时数据完好。拒绝也不改变 lease 的原始区域和 stride；覆盖参数仅属于该次提交。

提交顺序（每步不变式）：

1. **校验与快照**：在锁内验证 `life == BACKEND_RUNNING`、帧未封闭、此前无异步故障、lease 有效，并校验 `submit_area` 边界与 `src_stride` 容量不溢出。
2. **像素准备**：释放自旋锁，调用像素引擎完成必要的字节序翻转或 stride 紧凑化。
3. **二次故障检查与预登记**：重新加锁，检查转换期间是否有前序分块触发异步故障；若无，置 `SLOT_SUBMITTING`、分配单调 `cookie`、`++f->pending`、清除 `early_done`，随后释放锁。
4. **驱动提交**：调用 `panel.submit`，允许完成回调在函数返回前同步或从 ISR 到达（此时 `event_dispatch` 看到 `SLOT_SUBMITTING`，仅标记 `s->early_done = true`，不提前递减 `pending` 或释放 slot）。
5. **结果收敛**：重新加锁：
   - 若 `acceptance == PV2_TX_REJECTED && !s->early_done`：撤销预登记（`--f->pending`、`SLOT_LEASED`、`cookie = 0`），必要时回滚原地字节序，返回驱动错误码。
   - 其他情况（`ACCEPTED`、`UNCERTAIN`，或虽报 `REJECTED` 但 `early_done` 已发生）：消费 lease（`--f->leased`）；若 `UNCERTAIN` 或驱动附带错误码则记录 `f->result`；若 `early_done` 为真则立即调用 `complete_slot` 归还 `SLOT_FREE`，否则转入 `SLOT_INFLIGHT` 等待后续回调。

帧完成条件为“`sealed == true` 且 `pending == 0`”。第一块完成但帧尚未 `end` 时，绝不提前将整帧标为 `terminal`。

## Presenter 与 LVGL 的所有权边界

Presenter 维护一个活动 frame、一个可写 lease，以及有界的后端 receipt 集合。它不另外维护硬件 inflight 状态或释放后端 buffer。跨实例 lease、重复 submit、持有 lease 时 commit、非创建任务调用均拒绝。receipt 满时 begin 在既定预算内等待最早记录；完成后回收，避免正常连续刷屏耗尽槽位。

`drain` 不调用 backend.stop；它等待此前 commit 的 receipts，所有等待共享一个总超时预算，排空后仍可构建新帧。活动 frame 必须先 commit 或 cancel。cancel 超时保留上下文，后台完成后再次 cancel 收尾。stop 超时保留对象，成功后才可 destroy。

可选 LVGL 绑定的源缓冲与后端池分离：

```text
LVGL 绘制 → flush 读取自身 buffer → acquire 后端 lease → 复制/归一化颜色 → submit
                       ↓ CPU 读取结束                                  ↓
                 flush-ready 可重用源                           DMA 完成才复用 lease
```

一笔 flush 可以拆成多个 band，一次 refresh 的最后一笔 flush 才 commit。RGB888 通道转换在复制时进行；源的行 padding 不会进入发送数据。刷新中途失败会暂停 LVGL 定时器并取消活动帧，flush-ready 仍被调用，因为调用返回前已停止读取 LVGL 源。先前已接受的 DMA buffer 继续归后端持有；取消超时不影响这一隔离关系。

Host 使用真实 LVGL 软件绘制和延迟读取源指针的模拟 panel，验证 flush-ready 后覆写 LVGL buffer 不会改变待发送像素。此测试不等价于真实总线时序或硬件 cache 验证。

## TE 合成与发送

内部 `te_composer` 支持同步参考和异步路径：`IDLE → BUILDING → REPAIR → READY → INFLIGHT → IDLE`。同步 write 排空借用；异步 queue_write/finish_write 管理 ticket，seal 分段推进 repair；首帧缺少任何实际覆盖像素则保留 BUILDING，允许补齐后重试。READY/BUILDING 可取消；INFLIGHT 必须等待安全完成或明确拒收。成功完成才切换基线，失败保留旧基线。两张整帧、单个活动帧保证基线稳定，尚无跨帧引用计数。

任务侧 offer 检查 edge >= ready、now >= edge、now - edge < window，拒收后的重试不复用同一边沿。此 API 无 ISR 调用入口，也不会自动发送。后续调度只能提交最新边沿，并需按剩余扫描安全时间核对整帧发送预算；过了准入检查不等于已证明无撕裂。`te.c` 已将核心接入公共 backend descriptor。回调 mailbox 记录最新边沿、cookie 对应完成和 sticky fault；任务 service 处理 READY → SUBMITTING → INFLIGHT → IDLE。进入驱动前预登记 cookie，早到完成只置 done；驱动返回后再决定接受/拒收。UNCERTAIN 必须保留源直到完成或 quiesce；REJECTED 且无早到完成则回 READY，等新边沿。seal 之后丢弃 repair 期间收到的旧边沿。

stop 先置 CLOSING 阻止后续 service 发送，先 stop/join pixel worker，再以剩余预算调用 adapter.quiesce；超时不释放。安全 quiesce 后无完成证据的帧以失败终结，不提升基线。end 后不可 cancel_frame；未 end 的构建进入 CANCELING，等待已接受 copy 排空后回收；超时可重试。外部 TE 事件源的注销/退出由调用方负责，裸后端无 worker，Presenter 已提供可选后台 sender。以下异步流水已接入 TE（async_copy=true）；repair 已改为逐段 ticket 推进，不阻塞 service。

```text
BUILDING → COPY_DRAIN → REPAIR → READY → WAIT_TE → SENDING → RETIRED
```

1. **分块异步拷贝 (`BUILDING`)**：
   - tile 提交后由像素引擎写入合成缓冲（支持四方向旋转和字节序转换）。
   - 像素引擎通过 `pv2_pixel_config_t::on_event` 回调通知后端任务终结；后端依据 copy 完成回收 tile，不依据提交函数返回回收。
   - 同一目标区域的写入按提交顺序串行化；互不重叠区域或不同实例可并发排队。
2. **排空与前帧补齐 (`COPY_DRAIN → REPAIR → READY`)**：
   - `end` / `end_ex` 先封闭构建，等待所有已接受 copy 完成，再根据本帧累计提交区域（不信任 `end_ex` coverage 代替实际覆盖）计算未覆盖的 dirty 差集。
   - 补齐从稳定的有效前帧基线（`baseline_buffer`）读取，仅拷贝未覆盖区域；首帧没有基线时必须要求全覆盖或显式初始化，禁止读取未初始化内存。补齐完成后才进入 `READY`。
3. **TE 窗口准入与发送 (`WAIT_TE → SENDING → RETIRED`)**：
   - `WAIT_TE` 只负责发送准入（结合硬件 TE 边沿时间戳与相位窗口），禁止在 TE ISR 中执行图像合成或 repair。超出安全窗口则按配置等待下一周期 TE 或报错，禁止发送正在被 CPU/DMA 修改的合成缓冲。
   - `SENDING` 期间合成缓冲保持只读。DMA 传输完成后是否立即回池，取决于它是否还承担下一帧 repair 的基线：基线引用存在时保留，待新帧成为新基线且旧引用全部释放后才回收至 `FREE`。

## FB 扫描缓冲

当前 CPU 实现支持 1..3 FB 和 PARTITION/FULL/DIRECT；仅单 producer 构建，回调记录 cookie 相关确认，task service 处理切换。READY FIFO 只允许一笔 pending switch；三缓冲可以在前帧等待切换时继续构建下一帧。

PARTITION 使用独立 scratch，FULL/DIRECT acquire 暴露整个 candidate。后两种 submit 的 x/y 是整帧坐标，stride 必须为完整 surface stride。FULL 要求提交全屏；PARTITION/DIRECT 首帧要求覆盖全屏，后续从最新封帧基线预填，DIRECT 在 end 恢复未声明区域。外部 FB 为紧凑布局、借用而不释放，创建校验容量、对齐及互不重叠。

成功 SCANOUT_CONFIRMED 必须同时证明新扫描源生效和前扫描源不再读取。terminal receipt 结束 producer 借用，绝不自动将 current scanout 放回绘制池。普通 TRANSFER_DONE 被忽略。全局故障终结未发送帧，可能已接受的 switch 仍隔离到确认或 quiesce。确认带错误不视为安全切换证据，需要 stop 收尾。

单 FB 明确开启 allow_active_scanout_writes，取消不会回滚已写屏幕，后续强制全覆盖；多 FB 取消仅丢弃 candidate。单 FB 与多 FB 都要成功 quiesce 后才能销毁。

```text
FREE → DRAWING → PREPARED → SWITCH_PENDING → SCANNING → RETIRED → FREE
```

1. **三种绘制契约共用扫描所有权状态机**：
   - `PARTITION`（借用小块 scratch buffer 拷贝入 FB）、`FULL`（整帧渲染）、`DIRECT`（脏区直接渲染）在绘制及 `REPAIR` 策略上不同，但在 `PREPARED` 之后共用同一个扫描切换与退役流程。repair 和旋转在 `PREPARED` 前完成，不作为 facade 的独立 stage。
   - 支持通过 `pv2_backend_config_t::external_buffers` 注入由 LCD 驱动或 PSRAM 分配的外部帧缓冲数组。
2. **切换登记与退役判据 (`SWITCH_PENDING → SCANNING → RETIRED`)**：
   - 提交切换必须预登记新旧 buffer 的因果关系。
   - 平台确认新 buffer 生效（`PV2_EVENT_SCANOUT_CONFIRMED`），并满足旧 buffer 不再被扫描硬件读取的条件后，才能退役旧 buffer（`RETIRED → FREE`）。`transfer_done` 和 `scanout_confirmed` 的物理含义由适配层按具体 RGB / MIPI-DSI 面板定义，不对所有面板套用同一假设。
3. **Single-FB Persistent 独立契约**：
   - 仅有一个 FB 时，不能伪造 `FREE / DRAWING` 与 `SCANNING` 隔离的独占状态。只有平台允许直接写扫描源且使用者显式接受撕裂契约（`writes_active_scanout == true`）时才支持；否则在 `create` 时拒绝。

## 像素任务与超时

```text
NOT_ACCEPTED：参数非法 / 引擎关闭(INVALID_STATE) / 忙或记录满(NO_MEM) → 调用方保留内存
QUEUED / RUNNING：已接受 → 引擎持有 src/dst 借用
SUCCEEDED / FAILED：所有访问已结束 → 触发 on_event 通知，可释放借用与记录
```

1. **忙状态码与生命周期状态码分离**：
   - 引擎已调用 `stop` 进入关闭态时，`pv2_pixel_submit` 返回 `ESP_ERR_INVALID_STATE`。
   - 引擎正在执行任务或完成记录槽位已满时，返回 `ESP_ERR_NO_MEM`，以便调用方明确区分“可重试的背压/竞争”与“非法的生命周期状态”。
2. **原地转换与非重叠校验**：
   - 不同缓冲区的地址范围严禁重叠。
   - 仅当 `source.pixels == destination.pixels`、`rotation == PV2_ROTATE_0`、源区域起点与目标起点相同、且源与目标的 `layout`（宽高、stride、格式）完全一致时，允许原地字节序转换。
3. **完成与超时**：
   - `FAILED` 是安全终态：即使图像无效，硬件也已经停止访问内存。硬件是否停止仍不确定时只能保持 `RUNNING` 并置 `fault_pending = true`，禁止提前发布 `FAILED` 释放 buffer。
   - `pv2_pixel_wait` 超时返回 `ESP_ERR_TIMEOUT`，不改变任务状态。
4. **完成回调退出屏障**：
   - 同步模式 `on_event` 在 submit 返回前、引擎锁外执行；异步模式在 copy worker 中执行，status 指针仅在回调期间有效。回调可以 query/release 当前终态 ticket；若由回调释放，submit 返回的 token 已失效，必须明确由回调或提交者之一负责回收。
   - 引擎保持 busy 直到回调返回，回调内再次 submit 返回 `ESP_ERR_NO_MEM`。回调内 stop 返回 `ESP_ERR_INVALID_STATE` 且不改变关闭状态，防止等待自己退出。不得在回调内销毁引擎或等待未完成的其他工作。
   - 其他任务的 stop 等待像素处理和回调均退出；超时不能释放 user context。destroy 仍要求先 stop、释放记录并与所有 API 调用者完成外部同步。
5. **Stride 与 Cache 交接边界**：
   - DMA2D / PPA 的 stride 参数在像素引擎内由 byte stride 校验后转换成硬件参数；硬件无法表达的布局在提交前走 CPU fallback。初版要求 stride 为像素字节数的整数倍，不满足时统一拒绝（`ESP_ERR_INVALID_ARG`）。
   - **Cache 职责划分**：像素引擎负责 M2M 加速器（DMA2D / PPA）对 src 的 writeback 与 dst 的 invalidate/writeback；Panel Adapter 负责外设 DMA（SPI / I80 / RGB / DSI）读取发送缓冲前的 cache writeback。所有 DMA 缓冲按 64 字节 cache line 对齐分配，消除相邻槽位的 false sharing。

## 停止与销毁

```text
RUNNING → CLOSING → QUIESCENT → DESTROYED
                ↘ 超时：保持 CLOSING，资源仍有效，可重试 stop
```

1. `stop` 阻止新 `begin` / `acquire` / `submit`，自动取消尚未封闭的活跃帧构建（回收 `SLOT_LEASED`），调用 `panel.quiesce` 等待已接受的传输完成或由适配层安全中止硬件并解除回调。
2. 若 `quiesce` 因硬件中止而结束，仍在 `SLOT_INFLIGHT` 的槽位会被回收，同时将其所属帧的 `result` 标为 `ESP_ERR_INVALID_STATE`，绝不谎报帧发送成功。
3. 若 panel 是外部所有且框架无权停止扫描，适配层必须提供安全交接协议；否则 `stop` 返回超时/未完成，不能释放仍被扫描的内存。
4. `stop` 超时可继续 `query` / `wait` / 重试 `stop`；禁止 `destroy`。`destroy` 仅在 `QUIESCENT` 且所有外部发布的 receipt 均已调用 `release_receipt` 后执行（Fire-and-forget 的内部帧记录在 `stop` 成功时自动清理）。

## 实现验收场景

| 注入场景 | 必须验证 |
| --- | --- |
| 提交函数返回前完成回调 | 不重复释放、不覆盖终态、不漏记完成 |
| 第 N 块发送失败（驱动拒绝） | 已发块继续排空，未接受块 lease 保留（原地转换自动还原） |
| 帧内前序块异步故障后继续提交 | 拒绝后续 `acquire`/`submit_region`，可通过 `cancel_frame` 安全收尾 |
| 无外部 receipt 的 fire-and-forget 连续刷屏 | 超过 `max_frames` 帧后记录自动回收，不发生记录槽耗尽 |
| `cancel_frame` 0 超时后后台 ISR 完成 | 帧记录自动回收且不卡死新 `begin`，重复 `cancel_frame` 幂等返回 `ESP_OK` |
| `acquire(NULL)` 配合 `submit_region_ex` 目的区域/自定义 stride | 从源 buffer 起点按 stride 打包，逐字节校验第二行与 padding，并验证目的位置 |
| 无 receipt 帧发生异步错误后回收 | `get_fault` 仍保留首次失败，成功帧不覆盖 |
| 像素 callback 未退出时 stop | 超时且保留 user context；回调内 stop 不自锁 |
| 重叠区域连续合成 | 最终像素遵守提交顺序 |
| DMA 完成晚于 wait 超时 | buffer 保持借用，迟到回调只完成原任务 |
| CPU / DMA / PPA 混合处理相邻区域 | cache line 范围内没有丢更新 |
| 显示确认后仍持续 scanout | 当前 FB 不进入 free 池 |
| 首帧局部更新 | 明确拒绝或初始化，无未定义像素 |
| stop 超时后再完成 | 后续 stop 可成功，对象未提前销毁 |
| 旧 lease、旧 receipt、错误 cookie | 可检测，不能释放其他事务资源 |
| 多实例共享加速器 | 无串扰、忙时返回 `ESP_ERR_NO_MEM`、完成事件准确唤醒 |


## Presenter 的事件等待与 sender

启用 background_service 后，所有任务侧后端调用由 Presenter gate 串行化。panel callback 先更新后端，再在锁外分别通知 changed/work；外部 TE 边沿入口同样通知。sender 每次被唤醒调用一次后端 service，随后只通知 owner，不自唤醒空转。早到 panel 回调会保留 work，保证下一次迭代收尾。

owner 等待 receipt/lease/cancel 条件时先检查，未满足则释放 gate、等待 changed、重新获取 gate 并重查；原 timeout 的剩余预算贯穿循环。stop 请求 sender 退出，等待其 driver 调用返回并 join，然后才开始 backend stop/quiesce。任何阶段超时保留对象；成功 stop 后才能 destroy。外部事件源的退出屏障仍由调用方提供。

### FB 异步 PARTITION 的追加状态

`DRAWING → COPY_DRAIN → READY`：最后一笔 copy 未完成时 end 仅封闭构建并登记 receipt；scratch 和 candidate 继续借用，service 观察 ticket 终态后标记成功覆盖并完成封帧。新 begin 在 COPY_DRAIN 返回 NO_MEM，不能读取未完成 candidate 作基线。失败或实际覆盖不足终结 receipt 并保存 sticky fault，不提交 panel。

`DRAWING → CANCELING → FREE/SCANNING`：未封闭帧可取消，超时保留源与目标；service 在 copy 排空后回收，重复 cancel 幂等。单缓冲仍须显式允许扫描写入，取消无法撤销已可见的像素。stop join pixel callback 后才 quiesce panel。

### GRAM 可选异步分支

`LEASED → COPYING → READY → SUBMITTING → INFLIGHT → FREE`。默认同步分支不变；异步接受后立即减 leased、增 pending，copy 终态并不结束 panel 对转换缓冲的借用。任务 pump 选择最早 cookie，早期 copy 未完成直接让出，拒收留 READY 并返回错误，不允许后续块超车。

panel 提交前预登记 SUBMITTING；早到回调仍只置 early_done，panel 返回后统一回收。不确定接受不能退回 lease。取消/closing/fault 后，已经接受的 copy 继续排空，未发送块通过 complete_slot 回收；在途 panel 仍等回调/quiesce。stop 先 join pixel，再 quiesce panel，总预算不重置。


## 非阻塞 repair 与零拷贝补充

- TE：BUILDING → COPY_DRAIN → composer REPAIR（每次一个 ticket）→ READY → 新 TE 边沿 → INFLIGHT。repair 未完成时 seal 返回 TIMEOUT，service 保留上下文；禁止重新写入候选帧。最后段完成的时间才是新的 ready 时间。
- FB：begin 可启动 baseline→candidate prefill；acquire 在 ticket 完成前返回 TIMEOUT。DIRECT end 的未覆盖区域逐段 repair，旋转随后把逻辑候选转换到物理扫描池；全部完成才 READY、发布 baseline。
- stop 先 join pixel，再 quiesce panel；任何超时都保留 buffer。同步默认路径仍可直接完成 CPU repair。
- LVGL FULL 零拷贝：RENDER_START → begin/acquire → renderer 写 lease → 最后 flush submit/commit → 将 display buffer 解绑为私有安全源 → flush_ready。无脏区不触发 RENDER_START，不消耗 lease；REFR_READY 对异常中断时未提交的 lease 执行 cancel；binding 销毁不回收已封帧 backend 缓冲。
- 显式 Presenter observer 与内部 backend receipt 分开：内部 receipt 可在终态被自动回收，observer 保留状态快照；取消 observer 不改变 DMA 或 scanout 所有权。


## Review 后的资源与范围约束

旋转回零可释放逻辑绘制池，但 drain 只证明在途操作结束，不证明持续扫描停止；physical scan pool 与 SCANNING 状态保持，调用方重建 renderer 后必须全覆盖。硬件 cache 同步覆盖实际目标行带并首尾对齐，范围必须包含底层 PPA 会 invalidate 的行带且始终位于独占 allocation。coverage 优化只改变位图操作粒度，不改变实际覆盖判据。
