# 测试证据索引

当前网页：[RGB / MIPI / TE / SPI五板](./display-latest-20261111/README.md)。181项有效性能/内存，130项完整独立诊断；每项16场景。P4五项为PPA修复后新回归、TE16项为顶部标注复测、SPI为最近归档。另2项P4修复前诊断未完成，保留原始状态。源码/配置不同的来源分别标注，不宣称五板全部同版本重跑。

工程入口见 [dev/README.md](../README.md)。证据目录是某次测试的不可变记录，不是后续构建入口；其中绝对路径仅反映当时运行环境。没有本轮板测记录的改动，不沿用旧分数宣称已通过。

| 范围 | 记录 | 状态 / 适用范围 |
|---|---|---|
| 五板最新网页汇总 | [display-latest-20261111](display-latest-20261111/README.md) | 五板Tab、四角度、模式内存、静态SRAM、诊断/TE时序；不执行额外板测 |
| P4旋转PPA阻塞修复 | [p4-rotated-ppa-stall-20261111](p4-rotated-ppa-stall-20261111/README.md) | 独立重放18次复现，修复后20000次像素一致；5项80场景与44项host通过；干净IDF |
| MOSAICO TE 顶部标注复测 | [mosaico-te-visible-20261111](mosaico-te-visible-20261111/README.md) | 16组合/256场景，串口完整关闭；未确认目视无脏块，保留测试固件 |
| S31 RGB 单tile XIP性能定位 | [present-s31-rgb-analysis-20261109](present-s31-rgb-analysis-20261109/README.md) | 双方单tile；40项完整16场景；PARTIAL 0°/90°同开XIP后均37/36/40/40 FPS，同容量复核相同；burst/缓存精简无综合收益；双tile排除；默认配置未改，原固件整片恢复校验；未新增目视验收 |
| S3 FB_IRAM 当前配置开关A/B | [present-s3-fb-iram-on-off-20261109](present-s3-fb-iram-on-off-20261109/README.md) | SDK唯一差别为FB_IRAM；OFF完整24项、ON引用同日完整24项；内部堆+21.36KiB、PARTIAL/AUTO低1～3FPS；恢复原固件并整片校验；默认仍开启 |
| 11-09五板防撕裂/GRAM历史对照 | [five-board-regression-20261109](five-board-regression-20261109/README.md) | 生产源码b607bb9a1；present108、adapter73项通过，31项跳过；RGB/DSI排除NONE、mosaico仅TE、BOX-3 GRAM；四角度/四时点内存；五板原固件整片恢复校验；无新增目视验收 |
| 测试工程统一整理 | [unified-tests-20261106](unified-tests-20261106/README.md) | 40/40 Host、8个benchmark构建、3个专项构建；仅编译，未板测 |
| RGB NONE 等待与统计优化 | [rgb-none-profile-20261106](rgb-none-profile-20261106/README.md) | 同配置18→25FPS，补回历史cache/XIP后NONE41、DIRECT39；0°，非完整矩阵 |
| 最新架构 Host / 运行时旋转 | [architecture-20261106](architecture-20261106/README.md) | 修复逻辑/物理尺寸混用；当时37项Host，P4硬件跨轴复测尚待完成 |
| S3 RGB 原始 adapter 对比 | [rgb-adapter-compare-20261105](rgb-adapter-compare-20261105/README.md) | 原始完整模式四角度；其中DIRECT结果已由下一行替代，其他原始数据保留 |
| S3 RGB DIRECT 修复 | [direct-resolved-20261105](direct-resolved-20261105/README.md) | 2026-11-05历史DIRECT参考：present 39/30/32/31，adapter42/26/29/24；不是新架构改动后的重跑 |
| BOX-3 SPI GRAM 对比 | [box3-gram-20261105](box3-gram-20261105/README.md) | present20组/adapter4组，四角度与internal/PSRAM；不支持组合记 - |
| S31 QSPI TE 生命周期 | [s31-te-lifecycle-20261106](s31-te-lifecycle-20261106/README.md) | C1/C2×T1/T2，48帧及16生命周期通过；已恢复整片固件 |
| S31 QSPI TE LVGL benchmark | [s31-te-benchmark-20261106](s31-te-benchmark-20261106/README.md) | 8组/0°90°，10397次送帧，TE均值1.888ms、最大9.726ms；用户目视正常；无adapter C/T对比 |
| RGB/DSI NONE PARTITION统一 | [lcd-none-partition-20261108](lcd-none-partition-20261108/README.md) | Host 42/42；P4/S3 NONE 0°固件构建通过；P4未上板，不含新帧率结论 |

## 更早专项

其余子目录及 dev 下历史报告保留原日期和测试边界：fb-timing、fb-iram、fb-isr-join、gram-background、scanout 等用于追溯。旧报告中的“当前”、“V2”及已知失败应按对应日期理解，不自动覆盖较新的修复或验证状态。

## 新记录约定

使用 `<场景>-YYYYMMDD` 目录；同日多次以清楚后缀区分，保留旧记录。提交报告、manifest、原始日志压缩件、结果表和验证状态；保存指向维护工程入口的相对链接。整片 Flash 备份、build、依赖缓存不进仓库。记录实际板子 MAC、IDF/组件/场景版本、模式角度、目视反馈和恢复校验。每轮完成后更新此索引。

- [S3 RGB 同配置 adapter 对照与校验补齐](rgb-adapter-ab-20261106/README.md)：NONE 41/51、DIRECT 39/42 FPS，0°，见内部渲染路径差异说明。

- [Backend ops 第一批架构重构](backend-ops-refactor-20261106/README.md)：推进与可选调度创建入口收口；Host及跨芯片编译验证，非上板记录。

- [Engine完成跟踪与传输超时拆分](engine-tracking-refactor-20261106/README.md)：Host42/42、三芯片编译、S3 IRAM放置和Host内存规划对照，未上板。

- [模式解析规则收口](mode-policy-refactor-20261106/README.md)：Host43/43、62,208组配置前后快照一致、三芯片编译；未上板。

- [租约描述收敛与状态归属审计](lease-state-refactor-20261106/README.md)：保留逻辑撤销/物理排空差异，Host44/44、旧facade替换对照及跨芯片编译，未上板。

- [生产者交接提交点修复](producer-handoff-20261106/README.md)：跨线程prepare失败、drain超时与成功交接回归；Host44/44及三芯片编译，未上板。

- [私有源码目录整理](source-layout-20261106/README.md)：26个文件迁移、IDF/Host共享清单、源码逻辑不变核对，Host44/44与跨芯片编译；未上板。

- [私有头文件职责拆分](header-contracts-20261106/README.md)：43个声明保留、11个头文件独立C/C++编译与依赖边界检查、Host45/45、三类编译器布局对照及目标编译；未上板。

- [Backend类型、配置与操作表分离](backend-contracts-20261106/README.md)：三组声明原样保留、17个头文件独立C/C++检查、Host45/45、三类编译器16种类型布局对照及目标编译；未上板。

- [Pixel拆分与运行上下文字段分组](pixel-structure-20261106/README.md)：类型/无状态运算/生命周期分离、Host46/46、22个头文件检查、状态转换与CPU算法归一化对照、上下文尺寸及跨芯片验证；未上板。

- [架构重构后 RGB / DSI 硬件前后对照](refactor-hardware-ab-20261106/README.md)：112/112像素组合、16组LVGL benchmark；S3平均FPS一致，P4一组27→26其余一致；用户两屏目视正常，原固件整片恢复校验通过。

- [四板全模式帧率与内存 adapter 对照](all-modes-adapter-memory-20261107/README.md)：present92组合、adapter等价46组；P4两实现连续运行异常与逐组补测分别记录，四角度；内部RAM/PSRAM打开占用与局部最小剩余，原固件恢复校验；各模式支持边界和目视限制见报告。

- [S31 Korvo 原配800×480 RGB补测与adapter对照](s31-rgb-korvo-20261107/README.md)：bounce像素28组；原生bounce/non-bounce×真实1/2/3 FB×两核提交12组；四个0°模式综合场景双方完成。non-bounce仍未接入生产present，不能扩展为全模式长测验收。

- [S31 adapter 原生TE基线补测](s31-adapter-te-baselines-20261107/README.md)：9组有效成绩、3组同步软件旋转跳过；用户目视正常。补上此前adapter TE漏测，不映射为present C/T。mosaico原固件恢复未完成，按用户明确要求保持停止，备份完整保留。

- [DIRECT 优化与 S31 RGB 全模式、四角度对照](direct-borrowed-20261107/README.md)：源码 `4a9daccee`；present28项连续完成，adapter21项PASS、7项SKIP，另有独立插桩四项。0° 20→23 FPS，旋转退化实验已撤回。两块S31原固件均已整片恢复校验；前述TE归档中的恢复中断状态属于历史，当前恢复证据以本目录为准。新固件没有目视/光学撕裂验收。
