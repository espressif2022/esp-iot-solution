# S3 RGB LVGL benchmark：scanout 修复后模式与角度矩阵（2026-11-05）

设备：ESP32-S3 R16，GC9503 480×480 RGB，MAC 90:e5:b1:d0:a9:d8，USB0。
使用现有 examples/display/gui/lvgl_present_benchmark，完整 lv_demo_benchmark，每组 16 个场景，未缩短场景。RGB565，刷新周期 15ms，50 行单 tile，2 个 LVGL 软件绘制单元，RGB bounce 10 行，S3 FB_IRAM 开启，PSRAM 80MHz/XIP。实际驱动分配 3 个 FB，各模式使用其所需数量。
修复：提交成功后排空在途 ISR，成功后以一次后续 frame-complete 归还旧 FB；排空不可用/失败保守等两次。IDF esp_lcd 无修改。

## LVGL 汇总 FPS（单轮结果）

| 模式 | 0° | 90° | 180° | 270° |
|---|---:|---:|---:|---:|
| NONE | 25 | 15 | 14 | 15 |
| DOUBLE_FULL | 27 | 17 | 16 | 16 |
| TRIPLE_FULL | 37 | 19 | 19 | 19 |
| DOUBLE_DIRECT | 21 | 13 | 13 | 13 |
| DOUBLE_PARTIAL | 45 | 41 | 44 | 41 |
| TRIPLE_PARTIAL | 48 | 45 | 47 | 45 |
| AUTO | 48 | 45 | 48 | 45 |

## 传输完成计数 / 测量时间（次/秒）

| 模式 | 0° | 90° | 180° | 270° |
|---|---:|---:|---:|---:|
| NONE | 22.0 | 13.7 | 13.7 | 13.7 |
| DOUBLE_FULL | 23.9 | 15.8 | 15.6 | 15.8 |
| TRIPLE_FULL | 31.4 | 18.0 | 17.8 | 18.0 |
| DOUBLE_DIRECT | 18.1 | 11.8 | 12.5 | 11.8 |
| DOUBLE_PARTIAL | 30.5 | 28.0 | 30.2 | 28.1 |
| TRIPLE_PARTIAL | 33.3 | 30.6 | 32.8 | 30.6 |
| AUTO | 33.4 | 30.6 | 32.9 | 30.6 |

LVGL 汇总 FPS 是各场景统计的平均；transport_fps 是整个测量窗口内完成帧数除以耗时，两者不是同一指标，均非光学帧率。CPU 列为 LVGL 的 CPU 指标，不表示每核实测占用率。单轮结果不是统计性能保证。

完成 28/28 组；TE_SYNC 的单/双 compose × 四方向共 8 组在 RGB 上跳过，不作为 RGB 有效测试。完整检查见 validation.json，分场景数据见 scenes.csv，原始串口见 s3-rgb-live.log。

## 目视与验证边界

用户在红绿蓝场景报告过撕裂，报告时测试大致位于 NONE / 90° → 180° 阶段（无法对应精确帧）。NONE 明确允许写当前扫描缓冲，本身不保证无撕裂。原始观察见 optical-observations.json。双/三缓冲模式尚无逐项完整目视验收，不能从串口 PASS 推断光学无撕裂。
本轮未插入驱动地址诊断，也未做面板像素读回、FIFO/DMA 波形、restart 压力或逐帧退休地址验证。FULL/DIRECT 旋转和 DIRECT 的低吞吐仍需后续分析，不因矩阵完成而视为全部解决。

## 版本与恢复

仓库 HEAD：98d2f5c490b859ce1f524475df17ae4542d1b0f8（包含工作区修复，实际源码与固件 SHA256 见 manifest.json）；IDF：7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe。
测试前备份完整 16MiB Flash，并用设备 digest 校验一致。原分区表与 benchmark 不同，测试烧录 bootloader、分区表及 app；已恢复本次改写的首 1MiB，并独立校验完整 16MiB 与原备份 digest 一致；状态见 restore-status.json。原始 Flash 备份仅留在本地临时目录，不随证据上传或归档。
历史同工程结果 previous-project-run.txt 仅供参考，未作为本轮受控 A/B 的性能证明。

## 历史同工程 0° 结果参考（非本轮受控 A/B）

| 模式 | 上次 LVGL FPS | 本次 LVGL FPS |
|---|---:|---:|
| NONE | 25 | 25 |
| DOUBLE_FULL | 18 | 27 |
| TRIPLE_FULL | 29 | 37 |
| DOUBLE_DIRECT | 16 | 21 |
| DOUBLE_PARTIAL | 24 | 45 |
| TRIPLE_PARTIAL | 31 | 48 |
| AUTO | 31 | 48 |
