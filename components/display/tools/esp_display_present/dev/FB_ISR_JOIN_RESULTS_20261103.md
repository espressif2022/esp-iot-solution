# 标准驱动回调下的 FB 退休门控修复

后续 S3 剩余吞吐差距已通过组件热路径内部 RAM 放置处理，原版驱动 A/B 见 [最终复测](FB_IRAM_RESULTS_20261103.md)。本报告保留 ISR join 阶段的结果。

## 实现与范围

已修改生产 adapter：保留 V1 的“完成驱动提交后发布退休门控”顺序，用现有 `present_isr_barrier()` 排空提交前已经取样旧 FB 的 ISR，再允许一次 `frame_buf_complete` 确认切换。它借鉴提交发布协议，并非逐字搬运 V1 tracker；当前 backend 的单 pending slot 已保存新旧 FB 所有权，不再新增 inflight FIFO。

没有引入 tagged-retirement 驱动接口或修改 IDF 的切换／完成回调实现。测试沿用原复测的只读 LCD 能力查询补丁，未修改真实 IDF 工作区。旧驱动／配置若不能完成跨核 ISR join，继续等两次事件；单 FB 的曝光模式保持原策略。`draw_bitmap` 报错仍为不确定结果，不提前回收。

ISR join 在任务上下文运行，不持 adapter 自旋锁。只有 join 返回后才布防，因此提交过程中、join 过程中以及布防前到达的事件都不能归还旧 FB。下一次确认由 backend 原有流程把旧扫描 FB 标记为 FREE。前提仍为独占 panel、完整 driver-owned FB、DSI 连续扫描或 RGB bounce；不覆盖自定义 bounce、非 bounce RGB、在外部再次提交同一个 panel 等路径。

## 同条件合成吞吐

8 帧预热，连续提交 60 帧、末尾 drain，重复三次；RGB565、0°、16 行双 tile，生产者优先级 6，1 kHz tick。板卡、IDF 和负载与 [前次复测](FB_TIMING_V1_V2_20261103.md) 相同。这是生产者吞吐，不能解释为光学帧率或 LVGL benchmark。

| 场景 | V1 | 双回调基线 | 本次修复 |
| --- | ---: | ---: | ---: |
| P4 双 PARTIAL | 60.41 | 30.20 | 60.42 |
| P4 双 DIRECT | 30.20 | 20.13 | 30.20 |
| P4 三 PARTIAL | 60.39 | 30.20 | 60.40 |
| P4 三 FULL | 56.93 | 29.94 | 54.10 |
| S3 双 PARTIAL | 60.11 | 20.16 | 30.29 |
| S3 双 DIRECT | 60.08 | 30.03 | 56.12 |
| S3 三 PARTIAL | 59.46 | 30.03 | 50.13 |
| S3 三 FULL | 60.07 | 30.03 | 59.78 |

P4 的双 FB 主要差距已消除。S3 双 PARTIAL 仍有明显差距：本次每帧 begin 约 15 ms、acquire 约 12.6 ms、submit 约 2.3 ms；这些 API 的累计阻塞时间不能等同于后台 copy 的净执行时间，仍需进一步分段。不能把“单回调退休已实现”表述为“所有吞吐问题已解决”。

另在隔离副本对照同步 CPU copy、逐行 memcpy、仅在需要时发送 copy ticket／stop 通知、去除执行器重复 probe，四者的 S3 双 PARTIAL 均约 30.3，没有修复该差距，因此未带入生产代码。

## 独立诊断

每组预热 4 帧、提交 30 帧。四组均为 35 次确认（含初始化）、34 次旧 FB 释放，所有确认计数均为一次已布防回调；无记录丢失。检查器未报告读取期间释放或授予写权限。

| 场景 | 末次读取结束软件标记 → 释放，均值 |
| --- | ---: |
| P4 双 PARTIAL | 20.8 μs |
| P4 双 DIRECT | 29.5 μs |
| S3 双 PARTIAL | 40.2 μs |
| S3 双 DIRECT | 536.2 μs |

S3 PARTIAL 记录到一次 EOF 不足的 restart，原因位为 `0x4`；与前次诊断一样，插桩扰动不能排除，因此该组不能作为完整安全通过证据。S3 DIRECT 有一笔较晚释放，最大间隔约 16.6 ms。SUBMIT 到 ARM 包含驱动调用和 ISR join，诊断均值 P4 为约 47／169 μs，S3 为约 266／192 μs，不是单独测得的 IPC 开销。

这些是 ISR 内的软件标记，不是外部总线探针。没有做完整的带宽压力、重置竞态穷举和光学验证。

## Host 与设备恢复

Host＋LVGL 36/36，ASan/UBSan 36/36（`ASAN_OPTIONS=detect_leaks=0`，未验证泄漏检测）。回归覆盖提交期间回调、join 期间旧回调、join 不支持时双事件回退、驱动报错保持所有权、初始化、停止、保留扫描和解绑中的回调。

P4／S3 测试前均核验原有 16 MiB 完整备份；测试后恢复所有可能被固件写入的前缀，再独立校验整片 flash 与原备份一致。备份含设备数据，仅保存在本机，不归档。

源码快照、原始日志、插桩补丁、固件 SHA-256 及校验见 [证据目录](evidence/fb-isr-join-20261103/manifest.json)。
