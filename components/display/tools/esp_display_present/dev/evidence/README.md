# 测试证据索引

工程入口见 [dev/README.md](../README.md)。证据目录是某次测试的不可变记录，不是后续构建入口；其中绝对路径仅反映当时运行环境。没有本轮板测记录的改动，不沿用旧分数宣称已通过。

| 范围 | 记录 | 状态 / 适用范围 |
|---|---|---|
| 测试工程统一整理 | [unified-tests-20261106](unified-tests-20261106/README.md) | 40/40 Host、8个benchmark构建、3个专项构建；仅编译，未板测 |
| RGB NONE 等待与统计优化 | [rgb-none-profile-20261106](rgb-none-profile-20261106/README.md) | 同配置18→25FPS，补回历史cache/XIP后NONE41、DIRECT39；0°，非完整矩阵 |
| 最新架构 Host / 运行时旋转 | [architecture-20261106](architecture-20261106/README.md) | 修复逻辑/物理尺寸混用；当时37项Host，P4硬件跨轴复测尚待完成 |
| S3 RGB 原始 adapter 对比 | [rgb-adapter-compare-20261105](rgb-adapter-compare-20261105/README.md) | 原始完整模式四角度；其中DIRECT结果已由下一行替代，其他原始数据保留 |
| S3 RGB DIRECT 修复 | [direct-resolved-20261105](direct-resolved-20261105/README.md) | 当前DIRECT参考：present 39/30/32/31，adapter42/26/29/24；不是新架构改动后的重跑 |
| BOX-3 SPI GRAM 对比 | [box3-gram-20261105](box3-gram-20261105/README.md) | present20组/adapter4组，四角度与internal/PSRAM；不支持组合记 - |
| S31 QSPI TE 生命周期 | [s31-te-lifecycle-20261106](s31-te-lifecycle-20261106/README.md) | C1/C2×T1/T2，48帧及16生命周期通过；已恢复整片固件 |
| S31 QSPI TE LVGL benchmark | [s31-te-benchmark-20261106](s31-te-benchmark-20261106/README.md) | 8组/0°90°，10397次送帧，TE均值1.888ms、最大9.726ms；用户目视正常；无adapter C/T对比 |

## 更早专项

其余子目录及 dev 下历史报告保留原日期和测试边界：fb-timing、fb-iram、fb-isr-join、gram-background、scanout 等用于追溯。旧报告中的“当前”、“V2”及已知失败应按对应日期理解，不自动覆盖较新的修复或验证状态。

## 新记录约定

使用 `<场景>-YYYYMMDD` 目录；同日多次以清楚后缀区分，保留旧记录。提交报告、manifest、原始日志压缩件、结果表和验证状态；保存指向维护工程入口的相对链接。整片 Flash 备份、build、依赖缓存不进仓库。记录实际板子 MAC、IDF/组件/场景版本、模式角度、目视反馈和恢复校验。每轮完成后更新此索引。

- [S3 RGB 同配置 adapter 对照与校验补齐](rgb-adapter-ab-20261106/README.md)：NONE 41/51、DIRECT 39/42 FPS，0°，见内部渲染路径差异说明。

- [Backend ops 第一批架构重构](backend-ops-refactor-20261106/README.md)：推进与可选调度创建入口收口；Host及跨芯片编译验证，非上板记录。

- [Engine完成跟踪与传输超时拆分](engine-tracking-refactor-20261106/README.md)：Host42/42、三芯片编译、S3 IRAM放置和Host内存规划对照，未上板。
