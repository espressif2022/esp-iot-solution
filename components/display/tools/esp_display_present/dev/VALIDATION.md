# 当前提交：实板帧率与 Host 验证（2026-11-03）

后续三板模式／四方向矩阵和运行时旋转问题见 [模式与旋转专项结果](MODE_ROTATION_RESULTS_20261103.md)。
原始 V1 与当前组件的三板各模式／创建角度对照见 [V1／当前模式旋转结果](V1_CURRENT_MODE_ROTATION_20261103.md)；该表采用合成帧操作耗时，FB 的 quiesce 语义不同，不能与本页 LVGL FPS 混算。

实板固件基于 `8c69118a6182a83a22450710b06bc5a4563030b9`，并使用本轮板级测试头文件修复。跑分后工作区出现一处未提交的 `present_lcd_spi.c` 写法整理：最终 Host 重跑包含它，实板结果不包含它；两个文件 SHA 在证据清单中。另有未提交的测试工程和文档改动。V1 基线从 `41b0c9b9b6f72735cfa8878c159b1c25795b48e6` 导出到临时目录，未并入生产组件。

## LVGL 官方 benchmark

同一块板每个实现各运行一次，均为 16 个有效场景。**这是单次测量，不是统计平均或性能保证。** 每个数值来自 LVGL 汇总；场景 FPS 和时间取整，`0 ms` 不等于零耗时，LVGL CPU 指标不等于每核占用率。

| 板／显示 | V1 FPS | 当前 FPS | V1／当前 render ms | V1／当前 flush ms | V1／当前 LVGL CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| P4 MIPI 1024×600 | 27 | 27 | 15 / 16 | 0 / 4 | 47% / 61% |
| S3 RGB 480×480 | 19 | 17 | 52 / 52 | 4 / 6 | 86% / 89% |
| S31 QSPI 480×480 | 25 | 25 | 20 / 22 | 5 / 5 | 62% / 65% |

P4 和 S3 使用三帧缓冲 PARTIAL；S31 使用无 TE 的 GRAM。统一 RGB565、0°、2 块 50 行 tile、LVGL 9.5.0、33 ms 刷新周期、DPI 130、FreeRTOS 1000 Hz、性能优化编译；循环至少阻塞一个 tick。P4 CPU/PSRAM 为 400/200 MHz，S3 为 240/80 MHz，S31 为 320/200 MHz。两版同目标 `sdkconfig` 仅差当前组件的两个 Kconfig 配置项。33 ms 的周期限制了轻负载场景的刷新率，不能与历史 15 ms、16 行 tile 的 42/41 FPS 混为同一配置。

S31 的 TE 是另外的功能检查：配置 480×480 全屏，单 compose／0° 和双 compose／90° 各 12 帧完成，未对传输窗口插桩或进行光学撕裂测量；上表的 25 FPS 不是 TE 跑分。此前 64×16 ROI 只用于定时诊断，不纳入整屏 TE 验收。

### 逐场景 FPS

| 场景 | P4 V1 | P4 当前 | S3 V1 | S3 当前 | S31 V1 | S31 当前 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Empty screen | 26 | 26 | 21 | 17 | 26 | 26 |
| Moving wallpaper | 30 | 30 | 14 | 13 | 30 | 30 |
| Single rectangle | 30 | 30 | 30 | 30 | 30 | 30 |
| Multiple rectangles | 30 | 30 | 29 | 23 | 30 | 30 |
| Multiple RGB images | 30 | 30 | 28 | 26 | 30 | 30 |
| Multiple ARGB images | 30 | 30 | 26 | 22 | 30 | 30 |
| Rotated ARGB images | 30 | 30 | 29 | 29 | 30 | 30 |
| Multiple labels | 29 | 29 | 19 | 17 | 30 | 29 |
| Screen sized text | 16 | 16 | 6 | 6 | 14 | 13 |
| Multiple arcs | 29 | 29 | 30 | 29 | 30 | 29 |
| Containers | 30 | 30 | 24 | 24 | 29 | 30 |
| Containers with overlay | 30 | 30 | 11 | 10 | 20 | 19 |
| Containers with opa | 29 | 30 | 21 | 20 | 29 | 29 |
| Containers with opa_layer | 29 | 28 | 8 | 8 | 24 | 24 |
| Containers with scrolling | 30 | 30 | 8 | 8 | 18 | 17 |
| Widgets demo | 19 | 18 | 5 | 4 | 9 | 9 |

逐场景的 CPU、render、flush 数据在 [CSV](evidence/benchmark-20261103-head/scenes.csv)；[原始记录与散列清单](evidence/benchmark-20261103-head/manifest.json) 包含六轮串口文本、固件 SHA、配置及 SDK 关键源码 SHA。仅移除串口控制噪声，不改变场景数值。生成与构建步骤见 [板测说明](test_apps/legacy_compat/board_probe/README.md)。

## Host 与离线构建

| 配置 | 本轮结果 |
| --- | --- |
| Host + 真实 LVGL 9.5 | 35/35 |
| Host + LVGL，ASan/UBSan，`detect_leaks=1` | 35/35 |
| 不含 LVGL 的 Host | 31/31 |
| IDF 6.2 开发版：P4 / S3 / S31 benchmark 工程 | 均离线编译链接通过，并上板运行 |
| P4 / S31 板级内部功能工程、P4 scanout 事件插桩工程 | 离线编译链接通过；本轮没有运行插桩固件 |

Host 按 CTest suite 计数，不是断言数。测试分组如下：

| 分组 | CTest 项 | 验证重点 |
| --- | --- | --- |
| 图像与帧核心 | `image`、`frame_core`、`records`、`coverage`、`repair`、`plan` | 图像覆盖与修补、帧状态、资源计划与取消排空 |
| 后端与平台 | `gram`、`fb`、`te`、`te_composer`、`scanout`、`lcd_spi`、`rotation` | 缓冲轮转、异步完成／拒收、转换及生命周期 |
| 推进与边界 | `te_timing`、`presenter`、`presenter_te`、`transfer_timeout`、`event`、`scheduler`、`boundary_trace`、`boundary_analyze` | 窗口、背压、事件、超时和模拟驱动事件轨迹 |
| 像素与执行器 | `pixel`、`executor_cpu`、`executor_ppa`、`executor_dma2d`、`executor_both` | 独立链接、像素结果、准入和回退；本轮补回 cache 精确范围、紧凑源容量、DMA 忙重试及延迟完成时 stop |
| 应用与 LVGL | `legacy_resources`、`legacy_facade`、`legacy_te_gate`、`legacy_te_gate_old`、`lvgl_zero`、`lvgl`、`lvgl_te`、`lvgl_fb`、`headers_cpp` | 旧入口和资源、TE shim、真实 LVGL 与 C++ 头编译 |

此前 38 项中，两个 `pv2_*` 兼容 suite 随私有接口收敛移除，旧加速器包装 suite 被删除；本轮将其中仍有价值的硬件模拟回归补到现有执行器矩阵。35/35 与 38/38 不能当作同一测试集合比较。[普通 Host](evidence/benchmark-20261103-head/host-host.txt)、[sanitizer](evidence/benchmark-20261103-head/host-sanitize.txt)、[无 LVGL](evidence/benchmark-20261103-head/host-minimal.txt) 均保留输出。

## 兼容与边界

- 最新提交把原公开的 `esp_display_present_geometry.h`、`esp_display_present_point_t`、`esp_display_present_size_t` 改为私有，并移除了 `esp_display_present_geometry_map_logical_area_to_physical`。依赖这些接口的旧程序无法保持源码兼容；普通 presenter 和本轮 benchmark 的成功不能覆盖该情况。
- 三板测试前均全量备份并校验，测试后均恢复并独立校验 digest 一致；S31 首次独立校验发生 USB I/O 中断，重试后通过。本轮没有人工目视确认屏幕效果。
- S3 两版均有 GPIO3 被占用的驱动 warning；S31 重建测试实例时有 GPIO ISR service 已安装及 IO callback 覆盖日志。各轮仍完成 benchmark，不能仅凭完成标记消除这些日志背后的风险。
- TE 自动测量依赖面板时序假设；当前代码仍需进一步收敛物理整屏尺寸校验和 staging 分段发送语义。模拟结果和本轮日志不证明所有面板无撕裂。
- 使用带 RGB/SPI 只读能力查询改动的 ESP-IDF `7b9cc1ac79f8` dirty 工作区；Registry 发布、未打补丁 SDK、真实 underrun 和全部外部应用 ABI 均未在本轮验证。
