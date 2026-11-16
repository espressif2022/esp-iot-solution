# TE 时序与调度

TE 用一个线性扫描模型计算整帧的可发送窗口。普通 `esp_display_presenter_create` 装配 GPIO 测量、窗口计算和后台推进；图像 composer 只构建、固定和释放图像，不判断 TE 时序。

```text
GPIO 双边沿 + 传输完成时间 → 测量快照 → 窗口计算
                                      ↓
                     TE 输出：当前窗口准入或等待事件
                                      ↓
                       固定图像 → 面板提交 → 完成释放
```

相位从有效扫描起点计，周期为 P，扫描时长为 S，传输时长为 W。模型中的窗口为 `[max(S-W,0)+guard, P-max(W-S,0)-guard)`；当前时刻超出实测边沿的周期时等待新边沿。窗口尚未打开时，定时器只通知任务重新检查，不从回调发送；定时器迟到也不能绕过窗口检查。明确拒收消费该次边沿，重试需要更新的边沿。任务侧停止需排空计时器、复制与面板访问；超时保留实例以便重试。

自动测量根据 TE 两个边沿估计周期与脉冲宽度，并按总线速率和最近完成时间估计整帧传输时间。自动推导依赖具体面板的 TE 脉冲确实对应所配置的扫描相位、写入与扫描方向一致且传输近似线性。这些信息不能单凭 GPIO 信号证明；单次耗时或耗时上界也不能证明全速率范围内无撕裂。实际定时误差、面板扫描和光学效果需要板级验证。

当前内部代码仍有三种准入输入：自动测量使用 `te_gate`，内部配置也可提供模型参数或历史固定 `te_window_us`。这些不属于当前公开应用接口；虽然窗口计算已共享，准入路径尚未完全合一。`te_gate` 和显式扫描模型同时设置属于歧义配置，会被拒绝。

TE 准入仍按整张物理合成帧计算。内部 DMA 合成帧或已确认开启 PSRAM 直 DMA 时一次提交整帧；其余 PSRAM 合成帧使用两块内部 DMA staging，每块预算由 `CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES` 指定（默认 16 KiB）、按物理整行取整。已有 backend worker 预填另一块，当前片完成后再提交下一片，最多一片 DMA 在途；每片重新设置面板窗口，尾片不补齐线上数据。前台 service 可推进合成和回收，但不能消费 TE 准入边沿或进入发送；worker 提交时借出 engine gate，便于状态查询和有界停止。

分片会增加开窗、调度和复制开销。整帧实测传输时间包含这些间隙，并反馈后续准入估算；首次按总线速率估计仍可能偏乐观。总耗时只是模型输入，不能证明每行写入相对扫描的相位，因此双 staging 的防撕裂效果仍需目标面板验证，不能用 host 正确性测试替代。

停止与传输超时通过内部非阻塞中止请求关闭后续分片；正在复制或驱动内的片不会被强制释放。slot 仅在 `draw_bitmap` 返回且真实 color-done 到达后结束占用，整帧只发布一次逻辑完成。缺少最终回调时停止返回超时、保留实例与源帧，调用方不得释放 panel、IO 或缓冲。已写入面板的部分不会回滚。通用接口尚不能独立证明配置画布等于物理屏幕，物理整屏契约仍需板级约束；64×16 画布配合 panel gap 的诊断不算整屏 TE 功能验收。

旧版 ESP-IDF 缺少可排空回调的一次性定时器时，依赖相位定时器的配置会返回 `ESP_ERR_NOT_SUPPORTED`；不会用忙等代替安全退出。显式 service 模式仍需要调用方执行 service；后台模式由事件触发推进。硬件驱动不由定时器或 GPIO 回调直接发送。

Host 验证区分“尚未进入合法窗口”和“实测模型下不存在合法窗口”：前者返回
`ESP_ERR_TIMEOUT` 等待唤醒，后者由准入层返回 `ESP_ERR_NOT_SUPPORTED`，不再用
可重试等待掩盖不可能条件。该拒绝不是硬件传输完成，也不自动改变 backend 的
故障/回收生命周期。连续周期的模型测试不能证明真实面板脉冲与扫描相位一致。

### TE worker allocation

The factory uses one backend task for a single compose image (C1), including
its asynchronous pixel queue. Pixel execution occurs outside the engine gate.
Two compose images (C2) retain a dedicated pixel task so composition can overlap
an in-flight transfer. Tile count T1/T2 does not change this choice.
Both configurations retain the same admission, cancellation and completion
contracts; task sharing is not synchronous execution in the owner task.

The default input-tile budget is configured by
`CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES` (32 KiB). Non-direct PSRAM TE
uses two internal DMA stages, each bounded by
`CONFIG_ESP_DISPLAY_PRESENT_TE_STAGING_SLOT_BYTES` (16 KiB by default), rounded
to whole physical rows. Increasing stages may reduce inter-band overhead but
consumes more internal memory; the complete measured transfer must still fit
the TE timing model.

### Startup polarity observation

TE GPIO input is configured before polarity detection. Creation samples the
dominant level over approximately 9 ms before registering the pin ISR. This
avoids reversing blanking/scan when a single initial read lands in the short
TE pulse. It assumes a short blanking pulse and a longer idle scan, as the
existing automatic mode does. No task, heap pool or periodic sampling is added;
measured edge timing and the admission model are unchanged.
