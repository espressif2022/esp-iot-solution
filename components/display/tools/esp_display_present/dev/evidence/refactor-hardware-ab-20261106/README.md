# 架构重构后的 RGB / DSI 硬件前后对照

2026-11-06，维护入口：[dev/README.md](../../README.md)。本轮比较架构重构前后，不是 adapter 对比。

## 版本与可比性

- 基线：`e9ef0a64aac0320e768dd07165b4c93df349bfe5`，第一批架构拆分之前，已包含此前功能修复。
- 重构后：`d5b4d865a49f8c90d66cc135fad3c822b33aeae0`，第九批 Pixel 拆分与字段分组完成。
- 两版从对应 commit 导出生产源码，在隔离目录构建；共用重构后版本的维护测试工程。
- IDF：`7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，测试结束时 tracked 文件干净；LVGL `9.6.0~1`。
- 同板同场景的模式、角度、IDF、LVGL、sdkconfig 可比项、defaults、测试源码指纹一致，见 comparability.json 与各 manifest.json；生产源码逐文件匹配相应 commit，见 runtime-snapshot-validation.json。
- 唯一测试兼容调整是 executor_check.c 的私有 include 路径选择，补丁见 helper-include-compatibility.patch。最初漏了当前分支的 engine 头，导致一次当前 P4 像素工程编译失败；补齐后成功构建。两版测试源使用相同补丁，已跑过的六个固件经刷新构建元数据后 SHA256 不变，见 shim-refresh.json。生产代码未作临时修改。

## 板卡与覆盖

| 板卡 | 接口 / 分辨率 | MAC | 串口 |
|---|---|---|---|
| S3 LCD EV R16 / GC9503 | RGB / 480×480 | 90:e5:b1:d0:a9:d8 | USB0 |
| P4 Function EV rev3 / EK79007 | DSI / 1024×600 | e8:f6:0a:e7:a7:8d | ACM1 |

像素矩阵：每板每版本 NONE_1FB、DOUBLE_FULL、TRIPLE_FULL、DOUBLE_DIRECT、DOUBLE_PARTIAL、TRIPLE_PARTIAL、AUTO，各从 0°/90°/180°/270° 启动，共 **112/112 组合通过**。每组合提交4帧，包含中途旋转180°，总计448帧、112次运行时180°旋转；生命周期删除和 heap integrity 检查通过。本轮不代表跨轴运行时旋转覆盖。

LVGL 综合 benchmark：每板每版本 NONE、DOUBLE_DIRECT，各跑0°/90°，共16个组合，每组合16个有效场景。四次整套运行均完成，无记录到的 panic/assert。

## 帧率结果

下表是 LVGL 有效场景平均 FPS，非物理屏幕刷新率；未启用逐阶段 profiling。每配置仅一次运行。

| Board | Mode | Angle | Before FPS | After FPS | Delta |
|---|---|---:|---:|---:|---:|
| s3-rgb | NONE | 0 | 41 | 41 | +0 |
| s3-rgb | NONE | 90 | 20 | 20 | +0 |
| s3-rgb | DOUBLE_DIRECT | 0 | 39 | 39 | +0 |
| s3-rgb | DOUBLE_DIRECT | 90 | 31 | 31 | +0 |
| p4-dsi | NONE | 0 | 27 | 26 | -1 |
| p4-dsi | NONE | 90 | 18 | 18 | +0 |
| p4-dsi | DOUBLE_DIRECT | 0 | 26 | 26 | +0 |
| p4-dsi | DOUBLE_DIRECT | 90 | 20 | 20 | +0 |

S3 四组平均 FPS 与基线一致。P4 三组一致，NONE 0° 为27→26；保留这项差异，不凭单次整数均值判断显著性或归因。render/flush/CPU/transport 数据见 comparison.csv/json。

用户在最后 DIRECT 90° 场景期间反馈：“两块屏都正常”。这作为本轮目视结果记录；NONE 允许直接写扫描缓冲，其撕裂不计作本轮防撕裂失败。串口完成不等同于自动验证每一帧视觉正确。

## 恢复与证据

两板测试前均备份完整16 MiB Flash，结束后写回原镜像，并单独执行整片 verify-flash，均通过。restore/ 下保存原镜像哈希、恢复日志及校验结果。原始固件、build 和依赖缓存不进仓库。

baseline/、current/ 下保留每轮 manifest、结果、压缩的原始串口/构建/烧录日志；scripts/ 保存当时使用的编排和结果脚本，其中 /tmp 路径仅反映本次运行位置。SHA256SUMS 覆盖本目录其他证据文件。

本轮补足 RGB/DSI 对架构拆分的硬件回归，未重跑 TE/GRAM，也未做长时间压力与耐久验证。此前 Host46/46 和编译验证仍见[第九批证据](../pixel-structure-20261106/README.md)。
