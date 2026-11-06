# Backend ops 第一批重构验证 · 2026-11-06

实现范围与结构图见 [架构记录](../../ARCHITECTURE_REFACTOR_20261106.md)。本批是私有接口整理；应用层公共头文件未修改，尚未拆分 engine 源文件或合并状态。

Host：使用 ASan/UBSan、LVGL 9.6.0~1 的现有40项回归，40/40通过；包括RGB跨核屏障、FB/DIRECT补全、GRAM传输、TE手动/自主推进、提前回调、超时和停止。现有7个测试文件将具体 service 调用改为 backend ops.service，原场景和断言保留。

IDF：干净 `7b9cc1ac`，完整 benchmark + LVGL 链接检查。S3/P4构建 NONE/DOUBLE_DIRECT、0°/90°矩阵；S31构建 TE_C2_T2、0°/90°矩阵。构建结果见validation.json及各板manifest/build.log.gz。

本轮不烧录，不声称完成硬件视觉/帧率回归。构建矩阵用于编译和链接检查，不能记为上板通过。

一次直接从缓存目录名运行smoke的配置检查失败，原因是缓存名被识别为LVGL组件名；随后使用统一benchmark工具创建lvgl别名完成检查。没有修改依赖源码、IDF或为测试引入补丁。
