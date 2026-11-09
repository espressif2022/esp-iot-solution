# S31 RGB 性能定位（2026-11-09）

## 上板结论

所有正式对照固定单tile。按双0°、双90°、三0°、三90°顺序：XIP关时present为[17, 17, 24, 23]，adapter为[19, 24, 27, 29]；双方XIP开时present为[37, 36, 40, 40]，adapter为[37, 36, 40, 40]。

不开XIP时，burst128＋缓存精简的无计时复核为[17, 17, 24, 23]，与原始present对照。仅修改这些拷贝参数未追回原差距。

同开XIP后，adapter再把90°容量改为与present相同的48000B，成绩为[37, 36, 40, 40]。原始adapter的90°容量为80000B；因此原表同样50行不代表旋转后同容量。

XIP开关同时改变.text和.rodata访问位置；此次没有分别隔离取指、只读数据、I-cache冲突或任务调度成本，不能把现象进一步确定为某一种。897个LVGL对象的可重定位.text字节一致，也不代表最终链接地址和运行缓存行为一致。

本板实际为80MHz DIO Flash、200MHz Octal PSRAM、16KiB指令缓存；XIP开关同时移动代码和只读数据。数据确认了访问路径对成绩的影响，尚未单独量化取指与只读数据的贡献。

XIP开并非无内存代价：启动前PSRAM可用堆总量减少present 925.50KiB、adapter 903.38KiB；内部堆总量各少约10.44/10.42KiB。默认配置未改。

本结论限于这块S31 RGB的PARTIAL 0°/90°四项。未重测FULL/DIRECT及180°/270°，不把专项结果替换原五板总表。

固定生产源码 b3722b581（运行代码与原五板基线相同）；IDF干净无补丁。默认XIP关闭，专项同开另列；CPU320MHz、PSRAM200MHz。仅PARTIAL四项，顺序为双0/90、三0/90；每项完整16场景。

## 实验定义

baseline：原始present。burst128：只设DMA2D burst128，原值默认32。cache_contract：只精简重复维护；DMA2D使用IDF已有源写回和目标写回/失效契约；PPA保留目标预写回，源和完成后维护由驱动契约负责。burst_cache：二者组合。XIP组仅将双方XIP打开；adapter_matched以同开XIP的adapter为参照，仅在90°将buffer_height改30，使容量与present同为48000B；双方始终单tile。所有改动仅在隔离副本，未修改生产或IDF。

## 带计时结果

| 模式 | 角度 | 基线 | burst128 | 缓存精简 | 组合 |
|---|---:|---:|---:|---:|---:|
| DOUBLE_PARTIAL | 0 | 16 | 16 | 16 | 17 |
| DOUBLE_PARTIAL | 90 | 18 | 17 | 17 | 17 |
| TRIPLE_PARTIAL | 0 | 23 | 24 | 23 | 24 |
| TRIPLE_PARTIAL | 90 | 23 | 22 | 22 | 22 |

## 不带计时复核

| 模式 | 角度 | 基线 | 组合 | adapter |
|---|---:|---:|---:|---:|
| DOUBLE_PARTIAL | 0 | 17 | 17 | 19 |
| DOUBLE_PARTIAL | 90 | 17 | 17 | 24 |
| TRIPLE_PARTIAL | 0 | 24 | 24 | 27 |
| TRIPLE_PARTIAL | 90 | 23 | 23 | 29 |

## XIP同开对照（无计时）

| 模式 | 角度 | present关 | adapter关 | present开 | adapter开 |
|---|---:|---:|---:|---:|---:|
| DOUBLE_PARTIAL | 0 | 17 | 19 | 37 | 37 |
| DOUBLE_PARTIAL | 90 | 17 | 24 | 36 | 36 |
| TRIPLE_PARTIAL | 0 | 24 | 27 | 40 | 40 |
| TRIPLE_PARTIAL | 90 | 23 | 29 | 40 | 40 |

## 旋转缓冲容量对齐（双方XIP开、单tile、无计时）

| 模式 | 角度 | present | adapter原容量 | adapter同容量 |
|---|---:|---:|---:|---:|
| DOUBLE_PARTIAL | 0 | 37 | 37 | 37 |
| DOUBLE_PARTIAL | 90 | 36 | 36 | 36 |
| TRIPLE_PARTIAL | 0 | 40 | 40 | 40 |
| TRIPLE_PARTIAL | 90 | 40 | 40 | 40 |

按用户要求，所有正式对照固定双方单tile；双tile尝试已停止，仅存原始调试记录，不进入对比和结论。阶段计时嵌套/并行，不可相加。维护范围不是总线流量。控制组没有计时，不能把与profile版本差值都归于生产改动。单轮整数均值，不是重复交错统计检验。独立序列首项adapter与原全矩阵不同，原矩阵数据不覆盖。本轮只完成串口/统计验证，目视未重新验收。

原固件整片16MiB备份与校验通过；完成后完整恢复并独立校验。备份二进制与build目录不放交付件。

补充：上述相同整数综合分不代表逐场景完全收敛。[后续全面定位](../present-s31-deep-analysis-20261109/README.md)已分别统计空刷新、有效绘制、任务时间与缓存停顿，原始40项数据保持不变。
