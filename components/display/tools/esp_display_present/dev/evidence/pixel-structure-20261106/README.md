# Pixel职责与上下文结构整理 · 2026-11-06

基于585ecb047。将pixel基础类型、无状态几何/校验/CPU运算、ticket/worker生命周期分离。既有12个函数声明保留；backend类型和executor接口只引入pixel types。内部已校验CPU入口取代原static调用，不新增重复校验。

私有pixel engine字段按sync/lifecycle/notification/executor/worker归组，worker内归组priority；同步锁、回调重入判断、执行器借用、记录生命周期、取消/停止次序保持原样。没有新增任务或分配。count/records尾随存储保留；配置/请求描述符字段不改名。

## 验证

- 原runtime方法仅替换字段路径、校验与CPU调用名，归一化后与基线一致；CPU算法、几何与无状态包装归一化后相同。17个改路径叶字段通过编译期类型一致性检查；记录类型不变。
- Host ASan/UBSan与LVGL9.6.0~1：46/46通过，保留原异步取消、回调重入、停止等待、token与优先级测试。
- 新pixel_ops_standalone只链接ops实现，无OS/worker/engine库，覆盖非方形90°旋转及字节序、坐标变换、无效容量拒绝且目标不变、原地字节序转换。
- header_contracts扩展至22个头文件独立C11/C++17编译。临时include覆盖层模拟pixel types引入engine生命周期，测试按预期拒绝，生产源文件未修改；见negative-contract.log.gz。
- Host/Xtensa/RISC-V上，8种接口描述符、每类编译器59项尺寸/对齐/字段偏移/枚举值对照完全一致。私有上下文因分组改变布局，单独记录尺寸；不宣称私有对象二进制布局兼容。
- Host上下文136→144字节，copy_record为152字节且不变；三芯片上下文均64→64字节；实际编译参数下记录偏移均64→64、每条记录112→112字节，17个叶字段类型一致。具体见context-sizes.json与context-layouts.json。present_pixel_memory继续按sizeof计入预算。布局分组不能假定无填充成本。
- S3 RGB/P4 DSI benchmark含NONE、DOUBLE_DIRECT与0°/90°；S31 QSPI含TE_C2_T2与0°/90°。另编译S3 pixels并检查新CPU模块及原热路径共10个保留符号的IRAM放置；源码hash与manifest逐项核对。
- 公共头文件和IDF tracked文件保持干净。未烧录，无帧率/目视结论。

本轮只改变pixel职责组织和私有上下文字段路径，不修改backend配置结构或扫描缓冲交接。规则和层级图见[SOURCE_LAYOUT.md](../../SOURCE_LAYOUT.md)。
