# ESP-BOX-3 GRAM 与原始 adapter 对照 · 2026-11-05

本轮使用已提交的 present 修复（`85ae616826780fbd7c24ba7a841eea2cf41992e9`），组件源码未再修改；原始 `esp_lvgl_adapter` 0.7.4 与 HEAD 一致。present 20/20 组通过，adapter 4/4 个支持组合通过，共 384 条场景结果；两轮各启动一次，未记录 E 级错误、超时、断言或重启。每组均完成未修改的 LVGL 官方 benchmark 16 个有效场景，取 summary 的平均 FPS。

数据为单轮实测，不能把 1～3 FPS 差距解释成稳定领先。LVGL FPS 与 present 的 transport FPS 是不同指标；adapter 没有本轮可用的 transport 测量，未用其占位 0 值作比较。

下表各单元格为 **present / adapter 的 LVGL FPS**，`-` 表示不支持本轮相同的软件路径：

| 组合 | 0° | 90° | 180° | 270° |
|---|---:|---:|---:|---:|
| GRAM：内部 RAM，单 tile | 46 / 47 | 49 / - | 50 / - | 49 / - |
| GRAM：内部 RAM，双 tile | 52 / 52 | 51 / - | 53 / - | 51 / - |
| GRAM：PSRAM，单 tile | 50 / 47 | 49 / - | 50 / - | 49 / - |
| GRAM：PSRAM，双 tile | 53 / 51 | 51 / - | 53 / - | 51 / - |
| AUTO：内部 RAM，双 tile | 52 / - | 51 / - | 53 / - | 51 / - |

普通 GRAM 采用公开 `MODE_NONE`，单/双 tile 是 50 行绘制缓冲数量，不是 RGB/MIPI 的 framebuffer tear-avoid 模式。AUTO 在本板没有 TE 时解析到普通 GRAM，因此是策略入口验证；adapter 没有对应 AUTO API，记为 `-`。

旋转比较固定 panel 硬件方向：present 执行软件旋转，90°/270° 的逻辑尺寸为 240×320。原始 adapter 在 OTHER/SPI + NONE 的非零 rotation 配置下会警告“不处理 SPI 旋转”；它建议改用 panel 的 swap/mirror。为避免把没有正确旋转的跑分或不同的硬件旋转路径冒充同项对照，本轮将这些项记为 `-`，没有声称屏幕硬件本身不能旋转。对应源文件是 `components/display/tools/esp_lvgl_adapter/src/display/display_manager.c`。

| 额外模式（四角度） | present | adapter | 原因 |
|---|---|---|---|
| DOUBLE_FULL / TRIPLE_FULL / DOUBLE_DIRECT / DOUBLE_PARTIAL / TRIPLE_PARTIAL | - | - | 依赖 RGB/MIPI scanout framebuffer，不适用于本板 SPI GRAM |
| TE_SYNC（单/双 compose） | - | - | BOX-3 BSP 没有可用的屏幕 TE GPIO，本板未验证 TE 时序 |

两端相同条件：ACM0，MAC `3c:84:27:05:ba:78`，ESP32-S3 rev0.2，16 MB Flash / 16 MB PSRAM；320×240 SPI 屏，ESP-BOX-3 BSP ILI9341 初始化序列，SPI3 40 MHz、单线、queue depth 1；RGB565，50 行 tile。CPU 240 MHz，PSRAM 80 MHz/XIP，LVGL 9.6.0~1、双软件绘制单元、刷新 15 ms、DPI 60；LVGL worker 优先级 6、无绑核、24 KiB stack，性能优化编译。两端相关 sdkconfig 比较无差异，benchmark 源码 SHA256 相同，见 `config-comparison.json` 和 `manifest.json`。

present 使用默认关闭的 GRAM_BACKGROUND；0° 内部 RAM 是同步原地 swap / 直接 DMA，旋转与 PSRAM 走现有异步处理，PSRAM 配置使用内部 TX staging。这是现行资源策略，没有为跑分改动组件。BOX-3 屏幕/引脚依据本机 esp-bsp 的 esp-box-3 BSP 核对；触摸和音频不参与本轮。

用户在 adapter 前段运行时确认“正常，撕裂是正常的”。本轮按无 TE 的普通 SPI 吞吐验收，允许扫描期间更新 GRAM 引起撕裂；没有取得逐个角度的完整目视验收，也不作光学无撕裂声明，记录见 `optical-observations.json`。

测试前完整备份并独立 verify-flash 校验 16 MB Flash。测试结束恢复所有被覆盖的扇区，随后再次独立 verify-flash 校验完整 16 MB，digest matched。完整原始 Flash 不加入报告归档；本机备份位置及哈希见 `restore-plan.json`，恢复记录见 `restore.log`、`restored-verify.log`、`restore-status.json`。ESP-IDF 受版本控制文件无改动，LCD 未打补丁。

原始串口、每场景 CSV、配置/固件哈希、测试工程源码快照、构建/烧录/恢复日志和完整性校验随本报告保留。`benchmark-projects.tar.gz` 只包含隔离测试工程源码、配置与依赖锁文件，不包含 build、managed_components 或原始 Flash。
