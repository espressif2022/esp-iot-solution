# Engine 完成跟踪与传输超时拆分 · 2026-11-06

基于第一批 `9956efd6b`。把 engine 内的完成记录与传输时间戳拆成两个私有模块，保持公共 API、既有锁、事件顺序及资源释放合同。

## 结构与约束

- `present_transfer_tracker`：保留原超时策略及条件分配的时间戳数组；begin、拒绝回滚、迟到完成、到期收集、等待预算、quiesce后清理。借用原 control 锁，不分配新锁/任务，不持有像素，不调用 backend。
- `present_completion_tracker`：内嵌原 receipt/observer 数组、FIFO、累计完成计数和sticky first error。由 engine gate 保护；查询和释放 backend receipt 后才保存终态缓存，不把完成记录当作像素释放许可。
- Engine 继续掌握事件派发：到期FAULT先于真实DONE，所有 backend 回调与唤醒均在 control 锁外。gate借出/恢复、停止worker、driver quiesce及清理顺序保持原样。
- 原engine整对象的noflash映射补到两个新对象；中断完成函数定义保留IRAM属性。没有使用共享完整engine结构体的内部头文件来跨文件访问所有状态。

## 验证

- Host ASan/UBSan + LVGL9.6.0~1：42/42通过，原40项保留；新增2项分别检查超时边界/重复报告/迟到完成/cookie复用与FIFO阻塞/查询失败/释放失败/终态缓存。
- C++ header smoke包含两个新私有头文件，显式使用PRESENT_HOST，检查头文件与pthread路径兼容。
- S3 RGB、P4 DSI、S31 TE完整benchmark编译通过。S3/P4模式NONE/DOUBLE_DIRECT，0°/90°；S31 TE_C2_T2，0°/90°。矩阵只表示固件包含的组合，本轮未上板。
- 从S3最终ELF读取13个保留的tracker函数，均位于`.iram0.text`，见s3-iram-placement.json。
- 在同一Host工具链下，以基线engine对象与新engine库分别调用present_presenter_memory：关闭超时1800字节、开启超时2568字节，前后一致。它是Host内存规划估算，不是目标芯片实测堆用量。
- 公共应用头文件未改，IDF tracked文件干净。本轮无烧录、视觉或帧率结论。

编译过程发现并修复了两个移植声明问题：IRAM属性重复展开产生不同section；C++ Host目标新增OS头文件后缺少PRESENT_HOST。最终完整重跑通过。
