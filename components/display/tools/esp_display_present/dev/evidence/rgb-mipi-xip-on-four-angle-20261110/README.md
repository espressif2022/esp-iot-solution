# RGB / MIPI · XIP ON · 四角度正式对比

[网页](comparison.html) · [CSV](comparison.csv) · [完整来源](matrix-data.json) · [交接结果](findings.md)

## 执行范围

本轮重新完整运行0°、90°、180°、270°，不复用上一轮0°成绩。S3 RGB 480×480、S31 Korvo RGB 800×480、P4 MIPI DSI 1024×600；present六种防撕裂模式，adapter五种支持模式。NONE排除，adapter AUTO为−。仅XIP/FETCH_INSTRUCTIONS/RODATA开启；没有XIP OFF数据。

每板先跑干净IDF的present24项、adapter20项，随后独立跑同样44项轻量交接诊断。每一有效项从open开始，连续完整16个benchmark场景，关闭成功才计PASS。正式性能132项，诊断132项，共4224个完整场景；另有24个不支持条目。两个cohort不混成绩，不把264项都称为正式性能成绩。完整controller、manifest、case表、SDK配置、原始日志均归档。

源码运行时部分与上一轮验证版本一致，源版本c71871f433e34dc456970fb99702362b9a34f213。只增加测试角度与报告，没有生产交接修复。S3 bounce10行，S31不使用bounce；双方panel保留3张FB池，输入tile1张，PARTIAL按逻辑宽×50行分配（90/270°按旋转后宽度）。S3 present FB_IRAM开启，32KiB指令/64KiB数据缓存。双方及clean/diagnostic各板CPU/cache/PSRAM/LVGL公共有效配置验证一致。

## 数据定义

帧率取LVGL完整16场景综合分，不等于屏幕Hz；adapter旧transport计数0不作为零FPS证据。正式帧率、内存只取干净驱动轮；交接检测来自独立诊断轮。

内存仅四个剩余值：打开前、打开后、运行最小、关闭后；RAM/PSRAM分列，单位KiB。运行最小取after_run的*_min_free，而非当前free；两种堆的低点不一定同时发生。present列箭头相对adapter，帧率和剩余内存均越多越好。输入容量运行时核对，架构实际FB/旋转画布分配不人为填平。

## 检测语义与验证

S3读实际bb_fb_index；S31读AXI预取描述符所属FB，使用GDMA全局到AXI局部组号转换；P4独立保存每次实际装载的DMA帧索引，生产者目标后续变化不改这个快照。检查交还、取得可写、确认与LVGL渲染开始。adapter仅0°双缓冲FULL/DIRECT走独立确认等待；90/180/270°改走pipeline交还，验证要求有正数release覆盖，对应独立confirm标不适用，不能将0/0当成覆盖通过。后两种硬件观察不能冒充逐像素输出证据。unknown与池外独立输入另列，不能当作无异常。

RUN阶段门控以RGB VSYNC、DSI现有帧DMA完成，丢弃入口首半帧；不含open/close，包含benchmark结束统计日志。S3供数不足及既有自动重启、S31/P4硬件underrun分别计数。不添加故障、不assert、不控制修正、不额外等待、不替换回调、不算像素指纹。诊断有额外开销，独立于正式成绩。零命中不表示所有CPU/DMA写入与肉眼现象均被覆盖。

验证源码冻结、基础IDF干净、实际esp_lcd组件路径、全部XIP标志、有效公共配置、驱动原控制语句未删除/重排、消费方hook剥离后等同生产控制流、四角度case全集、16个唯一场景、非空绘制、tile容量、局部低水位及非零观察覆盖。ASan/UBSan主机测试验证正常/冲突/未知/阶段门控。浏览器独立核对1320个数值和480个内存箭头，无下拉框、手机/桌面无整页横向溢出。完整结果见validation.json。

## 归档

diagnostic-source.tar.gz为两轮实际组件/测试源码和完整隔离esp_lcd；observer.patch基于干净IDF。归档不含build、固件镜像、依赖缓存、flash备份。基础IDF与生产交接源码均未修改。按用户要求不恢复原固件，保留最后运行的XIP ON诊断固件。

上一轮0°证据单独保留；历史中的已捕获冲突不会因本轮未复现而视为已修复。
