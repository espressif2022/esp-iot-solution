# present / adapter 五板完整重测（2026-11-09）

生产源码固定为 `b607bb9a1`；IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，无补丁；LVGL `9.6.0~1`。

RGB / MIPI DSI NONE 不进入此次矩阵；mosaico 仅 TE；BOX-3 测 GRAM。每个有效组合一次完整16场景，不将失败前的成绩拼为通过。

| 板卡 | present 通过 | adapter 通过 | adapter 跳过 |
|---|---:|---:|---:|
| S3 RGB · 480×480 · R16 | 24 | 20 | 4 |
| S31 Korvo RGB · 800×480 | 24 | 20 | 4 |
| P4 MIPI DSI · 1024×600 | 24 | 20 | 4 |
| S31 mosaico QSPI · 480×480 · TE | 16 | 9 | 3 |
| ESP-BOX-3 SPI · 320×240 · GRAM | 20 | 4 | 16 |

FPS为单轮16个有效场景的整数均值，不是物理刷新率；小幅差值不代表统计显著提升。120条配置/角度记录，108项present有效成绩，73项adapter有效成绩。所有数据来自本轮，两实现输入、LVGL和公共配置指纹相同。TE原生adapter与present C/T分别列出，不强行等价。

## 内存口径

RGB/DSI公共硬件初始化统一创建3个driver framebuffer，让同一固件能连续测试双/三缓冲；内存反映该公共基线，不能当作独立双缓冲的最小硬件占用。四时点为打开前剩余、打开后剩余、运行最小剩余、关闭后剩余；RAM为INTERNAL|8BIT，PSRAM为SPIRAM|8BIT，不重叠。CSV/JSON保留原始字节；网页/Excel使用KiB。运行最小为各heap低水位之和，不保证同时发生。不是组件独占占用，也不是链接时静态数据区归因。

首项present在open内首次初始化LVGL，adapter在bench_init初始化；首项打开前边界不同，后三项更适合直接比较。关闭后不是全框架反初始化。

## 硬件与验收

S3更换为16MiB PSRAM的R16板，MAC `90:e5:b1:d0:a9:d8`，本轮不沿用旧8MiB板成绩。其余MAC见validation.json。五块板测试后均恢复运行前整片16MiB固件，并独立校验；Flash备份、固件与编译目录不放入交付件。

Host ASan/UBSan 47/47通过。P4 adapter DIRECT 270°本轮完整16场景通过（23 FPS）；旧报告的失败记录保留，本轮一次通过不代表已经确定旧失败根因。串口完成不代表已完成目视防撕裂验收。mosaico TE的数据发送时间与gate等待独立记录，TE放行并不自动保证整个传输落在消隐期内。

本轮显式用例表与收紧后的默认矩阵一致；默认 RGB/DSI 不含 NONE，显式专项仍可选择。测试规划器16项通过，统一runner的S31 RAM stub命令与本轮已验证运输命令一致，固件运行源码未因此更改。S31 RGB 的初次只读ROM备份因速度低主动停止，改用RAM stub完整重新读取。mosaico初次ROM读取在60KiB中断，未写测试固件；重试使用esptool5.4支持的RAM stub，仅调整下载运输方式，IDF/运行代码未改。初次失败日志保留。

## 文件

comparison-current.html：所有板卡默认并列，FPS和剩余内存差值都放在present单元格内；剩余越多越好，内存四时点直接并排。comparison-current.xlsx：同一份数据。comparison-current.csv / comparison-current.json：精确字节。raw-results.json：原始解析结果。manifests.json：构建/输入/固件指纹。validation.json：矩阵与恢复校验。

## S3 FB_IRAM 开关同表对比

S3 RGB 在原表直接并列 present IRAM开、present IRAM关、adapter；帧率与四时点RAM/PSRAM都按此顺序。两组present箭头均相对adapter，剩余越多越好。关闭组24项为本次补测，每项完整16场景；与开启组唯一SDK配置差异为该开关，生产源码与测试输入一致。关闭后内部堆增加21.36KiB；32KiB指令缓存＋PSRAM XIP配置下FULL/DIRECT持平或低1FPS，PARTIAL/AUTO低1～3FPS。单轮数据不代表统计显著差异。原固件已恢复校验，组件默认仍开启。其他板卡无此变体；不新增NONE模式。JSON/CSV增加present_off字段，Excel同表增加对应列；原始证据位于相邻present-s3-fb-iram-on-off-20261109目录。
