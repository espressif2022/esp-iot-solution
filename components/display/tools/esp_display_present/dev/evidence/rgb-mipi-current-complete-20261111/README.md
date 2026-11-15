# 三板 Present 与 adapter 实测

[对比网页](comparison.html) · [CSV](comparison.csv) · [原始数据与 manifest](matrix-data.json) · [串口记录](dma2d-ppa-20261115/)

2026-11-15 在 S3 Function EV、S31 Korvo 和 P4 EK79007 上使用 DMA2D/PPA 兼容版 Present 重新测量，adapter 也重新运行。六套固件都使用 ESP-IDF `fa8039b5cadb6e85dd830ff8c2c4bd73b6538aee`、LVGL 9.5.0、RGB565、PSRAM XIP 和 T1 逻辑 50 行配置。逐项核对了场景、输入缓冲大小和所在内存。

每板测量五个手动模式的全部四个角度以及 Present AUTO。adapter 的四个 AUTO 组合标为不支持。共 132 个通过组合、2,112 个有效场景，另有 12 项 AUTO 不支持。所有测值来自本轮。采集批次若有冷启动续测，会在网页和 manifest 里标注。

FPS 使用两端共同的 LVGL 场景口径。SRAM 静态段与内部堆分列，PSRAM 堆的容量和采样点来自固件；运行峰值取每项独立统计的堆低水位。所有数据均为整套测试固件开销。测试结束后验证三块板的原始整片 flash 与备份一致。

该目录保留 11 月 11 日的历史诊断附件。它们对应 `ef53e1b4` 下的旧报告。当前网页、CSV、JSON 与 `dma2d-ppa-20261115/` 对应本轮。
