# RGB NONE 等待修复与分段统计 · 2026-11-06

S3 LCD EV R16 / GC9503 480×480 RGB565、10行bounce、3个driver FB，MAC90:e5:b1:d0:a9:d8。IDF 7b9cc1ac、LVGL9.6.0~1、CPU240MHz、PSRAM80MHz、2个SW绘制单元。使用统一 benchmark 工程，默认统计关闭，调试版通过 --profile 启用。

## 分开验证的两项变化

| 组 | 单缓冲提交 | 指令缓存 / XIP | 统计 | NONE LVGL FPS | DIRECT LVGL FPS |
|---|---|---|---|---:|---:|
| A | 旧机制：不join，2个完成事件 | 16KB / off | on | 18 | 未测 |
| B | 修复：join成功后1事件，失败仍2事件 | 16KB / off | on | 25 | 未测 |
| B-control | 同B | 16KB / off | off | 25 | 23 |
| C | 同B | 32KB / on（恢复历史benchmark配置） | off | 41 | 39 |

每组完整跑 benchmark、每个case16个有效评分场景，非多轮统计置信区间。本轮没有跑新的原始adapter对照；不能把新数据直接和旧51FPS强行归因比较。DIRECT=39与历史同配置参考接近，但不是本轮独立adapter A/B。

A/B的board、case、IDF、场景源码、关键sdkconfig指纹逐项一致，只替换单缓冲post-submit join行为。A/B NONE begin平均10.000ms→2.718ms，最大42.298ms→25.763ms；LVGL render指标同为40ms，flush20ms→13ms；整轮提交14.6→18.7fps。单矩形29→59fps，Widgets7→7fps。begin为整个API壁钟等待，不是纯ISR/VSYNC等待。

B-control关闭统计仍为25FPS，表明本轮统计未改变最终整数FPS。C是另一次独立配置变化：render指标约40ms→16ms，不能把该收益归为join修复。芯片缓存/XIP参数从历史归档配置核对恢复，硬件时序没有改动。

## 安全与测量边界

单缓冲原来不调用ISR barrier，固定走2事件回退；现在成功提交后也join已进入的ISR，再等下一次完成事件。join超时/不可用仍保留2事件。2/3缓冲原已join，逻辑不变。Host补了单缓冲旧ISR/提前回调/成功、超时、不支持及真实单FB VSYNC分支，全部40项通过。

NONE本身允许写正在扫描的缓冲，本轮不是无撕裂证明。没有把raw VSYNC当作多缓冲退休，没有修改IDF。

CPU旋转路径已融合字节交换，未增加第二次转换。PPA SRM strict-MSPI拒绝与IDF一致，即使窗口对齐也不能放宽，新增Host测试保护CPU回退正确性。

分段统计只存在测试工程链接包装器；engine begin/acquire/submit/commit、executor_run CPU/hardware/fallback、cache维护、panel提交分别记录，嵌套/并行阶段不能求和。NONE/0°的executor复制数为0，CPU渲染发生在LVGL绘制层。cache_out为每帧2次全帧维护范围：adapter与IDF driver各一次；这不是2倍PSRAM真实流量，本轮未修改缓存归属。后续应按耗时再决定是否处理，不能据范围字节数宣称带宽收益。

首次统一入口还暴露并修复：RGB缺失已确认bounce配置导致创建拒绝；UART no-reset重连受前次ROM波特率影响。现在identity保留115200，UART操作显式reset，S3使用其支持的RAM flasher stub，S31保持no-stub。初次失败未作为通过记录。

原始整片备份保存在 /tmp/present-unified-validation-20261106/s3-rgb/backup/original.bin；备份SHA和恢复状态见validation及restore日志，不将Flash镜像纳入仓库。

恢复完成：原始16MB备份 SHA256 a1fb70defbe454c7a5d8fc233929fd9d9d39751910336c7baa96b905b60730fc，独立 verify-flash digest matched，随后复位。用户本轮目视反馈：正常。

新统计功能已在S31/RISC-V与TE统计同时启用的配置下编译通过，未烧录S31。
