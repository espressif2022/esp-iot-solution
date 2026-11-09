# FULL/PARTIAL 同容量重测（2026-11-09）

三块RGB/DSI板：DOUBLE_FULL、TRIPLE_FULL、DOUBLE_PARTIAL、TRIPLE_PARTIAL，0/90/180/270四角度；双方均跑完整16场景。S3额外重跑IRAM off，保持一张网页分开两表。BOX-3补充GRAM_T1_INTERNAL/PSRAM，adapter非0°不支持软件旋转，明确跳过。

122项有效成绩、6项跳过；66组双方有效且活跃源容量一致的对照；内存位置为首个flush记录。每个PARTIAL输入池只有一个tile，LVGL软件绘制线程数仍是两边相同的2。RGB/DSI无NONE。mosaico TE、DIRECT/AUTO及GRAM_T2在合并网页中保留历史标记，不宣称本轮重测。

PARTIAL按逻辑宽×50行×RGB565：S3始终48000B；S31 0/180为80000B、90/270为48000B；P4分别102400B/60000B。本轮对齐方向是缩小原adapter的旋转输入容量以匹配present默认值，不是增大present。FULL每张整帧为物理宽×高×2，不随旋转变化。

adapter公开buffer_height以物理宽计数，无法用整数行精确表达P4的60000B。测试只在隔离副本替换PARTIAL分配表达式，framebuffer退役、复制和同步逻辑不变。源码补丁及构建指纹保存。正式工具提交c3bf82364将这项处理限定在构建overlay中，默认logical-50，可选legacy-physical-50复现旧分配；checkout中的adapter和IDF不变。

BUFFER_AUDIT记录活跃源容量、首个flush的内存位置、刷新/非空绘制/flush次数和像素量；每项结束、显示关闭后才打印，没有逐帧日志。容量每次flush都检查。input_tiles字段是测试配置，FULL不使用它表示descriptor或panel buffer数量。FULL的借用、保底存储和旋转画布差异仍计入整机内存，不人为额外分配来填平。

主表帧率是LVGL16场景整数均值，可能包含空刷新；另列非空绘制/秒，仍不是实际扫描完成率。原CPU100%不作为真实负载。内存沿用四时点：打开前剩余、打开后剩余、运行最小剩余、关闭后剩余；KiB显示，CSV/JSON保存精确bytes；剩余越多越好。不同heap低水位不保证同时发生，不等于静态段占用。首项present在open内初始化LVGL，而adapter在bench_init初始化；首项打开前边界不同，关闭后也不是整个框架反初始化。

本轮保留各板原比较配置的XIP与时钟，只隔离分配策略；S31双方XIP关闭。新子矩阵顺序与旧完整矩阵不同，并加入相同轻量事件核对，因此旧新变化不能完全归因于tile大小；只做一轮，没有交错重复的显著性分析。

运行代码以d84edb1d0为基准，与c3bf82364生产源码一致；两者差别是测试工具。P4正式运行先从DOUBLE_FULL开始；用户先前看到的NONE来自11月4日保留的lvgl_present_benchmark原固件。每块板结束恢复运行前原始固件并独立整片校验，因此P4原固件仍可能显示其旧NONE用例，与本轮矩阵区分。

两路UART首次只读备份中断，未写入。随后将当前整片Flash与此前同MAC、已完整恢复校验的备份核对，一致后才复用。失败记录保留；没有使用半份备份。其余两块新备份完整读取并校验。交付不含build/固件二进制；串口通过不替代目视防撕裂验收。

文件：comparison.html为同网页并排对比；comparison-current.json/csv为合并的新旧数据，cohort区分来源；aligned-raw-results.json仅本轮；buffer-verification.json为逐项容量与位置核对；evidence保留日志、指纹和恢复证明。
