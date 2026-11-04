# P4 DSI 带提交身份的退休实验

这是实验补丁，**不属于生产实现**。`prototype.diff` 以 IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 的隔离副本（额外应用复测用只读能力补丁）和当前 presenter 工作区为基础。补丁涉及 `esp_lcd`、adapter 和 Host mock 头；不会自动应用到正常构建。

实验将 FB 与 presenter cookie 一起交给驱动，驱动在 DMA 完成处理时记录旧 FB、启用下一链表并携带 cookie 回调；adapter 匹配 cookie、新 FB 和上次确认 FB。实验假定 presenter 独占 panel；重置、其他调用者切换 FB、回调解绑和停止恢复等尚未完成完整设计。仅编译通过和正常画面不足以证明硬件不再读取旧 FB，不能用于发布。

P4 1024×600、RGB565、16 行双 tile，同复测报告的合成负载：8 帧预热、60 帧、重复三轮，末尾 drain。未插桩性能样本均值：双 FB PARTIAL 60.42、三 FB PARTIAL 60.40、双 FB DIRECT 30.20、三 FB FULL 54.92 帧／秒。它们是生产者吞吐，并非屏幕光学帧率。对照基线分别为 30.20、30.20、20.13、29.94；V1 分别为 60.41、60.39、30.20、56.93，详见 [复测报告](../../FB_TIMING_V1_V2_20261103.md)。

`test_tagged.c` 是针对实验 adapter 的 Host mock，覆盖错误代次、错误目标、重复及提交函数返回前回调；它不测试真实驱动或并发。继续工作包括：扩展读取和写权限轨迹、错误与 reset 场景、缓存与硬件契约审查、长时间压力，以及 S3 RGB bounce 的独立实现和验证。

P4 单独运行带插桩诊断：双 FB PARTIAL／DIRECT 各预热 4 帧、提交 30 帧，记录器无丢失；两组各 34 次旧 FB 释放。软件标记的最后 DMA 读取结束到开放旧 FB，平均分别为 20.2／35.3 μs，检查器未报告读取中释放或写权限授予。见 [原始轨迹](p4-trace.log)、[分析](p4-trace-analysis.json) 和 [插桩补丁](trace-hooks.diff)。这些标记在驱动 ISR 内，不是总线探针；本轮没有加入 underrun 标记，也没有做带宽压力、重置和并发穷举，不能将此结果作为完整安全验收。

性能固件 SHA-256 `9cd1e73ef3663877e590741ff56825f4e754f612e556a682d5bc83ea15dfbcb5`；诊断固件 SHA-256 `7d43deb90739e1200dfba3393ed9658a8b657f51337b0b5534c76257575496fa`。原始性能样本见 [p4-perf.log](p4-perf.log)。

两次测试前校验原有整片备份，测试结束均恢复 P4 16 MiB 镜像，并以独立 `verify-flash` 校验一致。备份含设备数据，仅保留在本机临时目录。
