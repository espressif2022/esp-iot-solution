# 首轮上板验收 · 2026-10-30

范围：V2 Presenter → FB → LCD scanout；CPU、RGB565、DOUBLE_FULL、零旋转。
S3 480×480 RGB bounce，P4 1024×600 MIPI-DSI。两块板完整原 Flash 已备份；
本地 ESP-IDF 6.2 构建通过，esptool 写入校验通过。未修改 V2 生产代码。

| 板子 | 日志确认帧范围 | 最大等待时间 | 采样内部空闲 heap | pending / sticky | 画面 |
|---|---|---|---|---|---|
| S3 | 0..240 | 34055 µs | 300635 B | 0 / ESP_OK | 用户确认正常 |
| P4 | 0..240 | 34324 µs | 513363 B | 0 / ESP_OK | 用户确认正常 |

frame 从 0 开始，每帧等待实际 PRESENT_CONFIRMED，再释放 receipt；前 8 帧与每
120 帧打印日志。因此记录证明至少 241 次确认，非只有打印的 10 次。两板各抓取
45 秒控制台，采样 heap 不变，未发现 FAIL、assert 或崩溃。首次提交后的等待也
无需第二次提交即可完成，验证了两次帧事件确认路径在当前驱动/板子上能够推进。

用户确认纯色、彩条和移动图案显示正常。这是有限时长、目视的基线验收，不是
光学无撕裂证明、并发穷举或长期内存泄漏证明。等待时间包含两次帧边界与调度，
不是像素绘制耗时或吞吐测试。S3 初始化报告 GPIO3 已配置的警告（BSP 要求先
拉高 VSYNC）；后续初始化与帧确认成功，未将该警告作为隐藏失败忽略。

本地证据位于忽略目录 `artifacts/`：`s3-run1.log`、`p4-run1.log`、两板 build/flash
日志、`acceptance.json`（本次烧入 bin 的 SHA256）、`original-backups.json` 及
完整备份。源码在采集后仅按仓库风格格式化，记录的哈希仍对应本次实际烧入文件。

下一轮依次验收：PARTIAL / DIRECT、三缓冲、LVGL、stop/recreate、旋转/触摸、
硬件加速和长时间压力。GRAM/TE 需要相应接口屏幕，不能用本次 RGB/MIPI 结果替代。

## 第二轮：五模式与停止重建 · 2026-10-30

两板均重新编译、烧录并核对 MAC，写入哈希校验成功。V2 生产代码未改动。
测试范围为同步 CPU、RGB565、零旋转，每项 48 帧后真实停止/销毁面板及
Presenter，再释放板级总线/IO/expander/LDO，重新创建下一模式。

| 模式 | S3：确认帧数 / 最大 commit→确认时间 | P4：确认帧数 / 最大 commit→确认时间 | 整帧像素对照 |
|---|---|---|---|
| DOUBLE_FULL | 144 帧 / 34.12 ms | 96 帧 / 30.19 ms | 通过 |
| TRIPLE_FULL | 144 帧 / 34.11 ms | 96 帧 / 30.18 ms | 通过 |
| DOUBLE_DIRECT | 144 帧 / 65.81 ms | 96 帧 / 68.35 ms | 通过 |
| DOUBLE_PARTIAL | 144 帧 / 30.73 ms | 96 帧 / 33.60 ms | 通过 |
| TRIPLE_PARTIAL | 144 帧 / 30.75 ms | 96 帧 / 33.62 ms | 通过 |

- S3 完成 3 轮，共 720 帧；P4 最终修正版完成 2 轮，共 480 帧。只统计完整循环，尾部未完循环不计入。
- 每帧要求 PRESENT_CONFIRMED、terminal、ESP_OK；无 pending receipt 遗留或 sticky fault。
- DIRECT/PARTIAL 首帧全屏覆盖，随后更新旧/新两个 37×59 脏矩形；PARTIAL 以 11 行拆 band，脏区源 stride 带 3 像素 padding。逐帧核对整个提交缓冲，包含脏区之外的保留内容。
- 每项 stop/destroy 成功，heap 完整性检查成功。销毁后 S3 内部空闲 353087 B、PSRAM 16774784 B；P4 分别 594891 B、33551868 B，已完成轮次/模式之间均一致。
- 用户对两板各模式目视反馈“OK”。切换模式时短暂黑屏是实际删除并重新初始化屏幕的预期行为。
- 初轮 P4 暴露测试工程未释放背光 GPIO26 的重复配置警告；补上 `gpio_reset_pin(26)` 后重新烧录，最终记录未再出现该警告。未改 V2 状态机。

证据：`artifacts/s3-suite2.log`、`p4-suite2-final.log`、两份 `*-suite2-summary.json`、
`*-suite2-build.log`、`*-suite2-flash.log`、`suite2-firmware.json`（实际烧入固件及测试源码 SHA256）。
P4 初轮 `p4-suite2.log` 仅保留定位警告的过程，不计入最终表格。

**验收边界**：每帧等待完成后才继续。三缓冲验证配置/分配与生命周期，尚未验证
第三块缓冲必然被使用、满队列、背压或连续流水吞吐。逐像素对照读取 CPU 可见
的提交缓冲，不等同于独立的 DMA 总线/光学采样。有限轮次 heap 稳定不代表长期无泄漏。

下一轮：三缓冲排队/背压与实际第三缓冲使用 → LVGL PARTIAL/FULL/DIRECT →
旋转/触摸 → DMA2D/PPA 对照及长时压力。RGB888、SPI/TE 实屏仍需独立验收。
