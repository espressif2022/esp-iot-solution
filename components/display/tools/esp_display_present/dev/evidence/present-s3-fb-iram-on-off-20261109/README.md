# S3 FB_IRAM 当前版本开关对照（2026-11-09）

生产源码 `b607bb9a1`；同板MAC `90:e5:b1:d0:a9:d8`，480×480 RGB，80MHz PSRAM、32KiB指令缓存、XIP。IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 干净无补丁。LVGL 9.6.0~1。

ON来自今天五板测试的完整24项，OFF本轮重新烧录完整24项，每项完整16场景；未缩短场景。全SDK唯一差别为FB_IRAM y/n，生产文件与共享测试源码指纹相同。单轮结果，不是交错重复的统计检验。

内部堆总容量OFF−ON：21872 B（21.36KiB）；PSRAM总容量差：-160B。

| 模式 | 角度 | 开启 | 关闭 | adapter参考 |
|---|---:|---:|---:|---:|
| DOUBLE_FULL | 0° | 27 | 27 | 26 |
| DOUBLE_FULL | 90° | 17 | 17 | 15 |
| DOUBLE_FULL | 180° | 17 | 17 | 16 |
| DOUBLE_FULL | 270° | 17 | 17 | 15 |
| TRIPLE_FULL | 0° | 37 | 36 | 37 |
| TRIPLE_FULL | 90° | 19 | 19 | 15 |
| TRIPLE_FULL | 180° | 19 | 19 | 16 |
| TRIPLE_FULL | 270° | 19 | 19 | 15 |
| DOUBLE_DIRECT | 0° | 43 | 43 | 42 |
| DOUBLE_DIRECT | 90° | 31 | 30 | 26 |
| DOUBLE_DIRECT | 180° | 32 | 32 | 29 |
| DOUBLE_DIRECT | 270° | 31 | 30 | 26 |
| DOUBLE_PARTIAL | 0° | 44 | 41 | 42 |
| DOUBLE_PARTIAL | 90° | 41 | 38 | 37 |
| DOUBLE_PARTIAL | 180° | 44 | 41 | 42 |
| DOUBLE_PARTIAL | 270° | 40 | 38 | 37 |
| TRIPLE_PARTIAL | 0° | 48 | 46 | 47 |
| TRIPLE_PARTIAL | 90° | 45 | 43 | 42 |
| TRIPLE_PARTIAL | 180° | 47 | 46 | 46 |
| TRIPLE_PARTIAL | 270° | 45 | 43 | 42 |
| AUTO | 0° | 48 | 46 | - |
| AUTO | 90° | 45 | 43 | - |
| AUTO | 180° | 47 | 46 | - |
| AUTO | 270° | 45 | 43 | - |

RAM与PSRAM保留打开前、打开后、运行最小、关闭后四时点，原始字节见CSV/JSON；网页KiB，OFF单元格的差值相对ON，剩余更多为好。内存是统一3个driver FB的整机测试基线，不是组件独占占用或独立模式最小配置。两份均为present，首次初始化边界一致。

本次关闭只作用于隔离测试工程，组件默认未修改。开关关闭不移除源码显式IRAM_ATTR；完整map压缩件和段统计保留。当前结果不能直接替代2026-11-03的16KiB指令缓存合成吞吐数据。

原固件重新整片备份并独立校验后才写测试固件；测试完成恢复同一份16MiB并独立校验。Flash备份、固件和构建目录不入交付件。没有新增目视/光学防撕裂验收。
