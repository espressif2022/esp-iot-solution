# 当前源码交接与供数诊断

只记录；正式帧率和内存不取诊断轮。adapter 已测项经运行源码、配置和观察点核对复用；present 是本次重新采集的完整诊断。零命中不证明任意条件下无撕裂，unknown 不当作正常。

## s3-rgb

检查实际用于bounce取数的PSRAM framebuffer地址。

- present：地址命中 0，unknown 0，硬件 underrun 0。已覆盖检查点无地址命中。
  RUN 供数不足 174/100564 帧，既有自动重启 174。
- adapter：地址命中 4，unknown 0，硬件 underrun 0。DOUBLE_FULL 0°：2；DOUBLE_DIRECT 0°：2
  RUN 供数不足 96/83827 帧，既有自动重启 96。

## s31-rgb

检查AXI预取描述符所属framebuffer；属于保守的DMA借用地址核对，不等于逐像素扫描位置。

- present：地址命中 0，unknown 0，硬件 underrun 0。已覆盖检查点无地址命中。
- adapter：地址命中 0，unknown 0，硬件 underrun 0。已覆盖检查点无地址命中。

## p4-dsi

检查驱动实际装载到DMA的帧地址独立快照；记录LLP作上下文，不等于逐像素扫描位置。

- present：地址命中 0，unknown 0，硬件 underrun 0。已覆盖检查点无地址命中。
- adapter：地址命中 6，unknown 0，硬件 underrun 0。DOUBLE_FULL 90°：1；TRIPLE_FULL 0°：1；TRIPLE_FULL 90°：1；DOUBLE_DIRECT 0°：2；TRIPLE_PARTIAL 0°：1

检查点命中不等于独立撕裂次数。S3 检查实际 bounce 源；S31 为 AXI 预取所属 FB；P4 为实际装载帧快照，后两者不是逐像素输出证据。原始逐项计数与地址样本保留在 matrix-data.json。

补测 P4 曾在进入 app_main 前出现一次启动异常；原始日志单独保留在 diagnostics/p4-clean-startup-fault。相同固件复位后从首项重新开始；随后按 case-ledger 逐项保留实际完成与失败记录，未将中途失败的批次当作整套成功。启动异常未进入测例，不计分。

## 未成功完成的当前组合

- p4-dsi / present / clean / DOUBLE_DIRECT 270°：bench_close ESP_ERR_TIMEOUT; no valid FPS
- p4-dsi / present / clean / DOUBLE_PARTIAL 90°：bench_close ESP_ERR_INVALID_STATE; drawing stopped; no valid FPS
- p4-dsi / present / clean / DOUBLE_PARTIAL 270°：Run failed before successful close; ESP_ERROR_CHECK failed: esp_err_t 0x103 (ESP_ERR_INVALID_STATE) at 0x480087ea
- p4-dsi / present / clean / AUTO 270°：Run failed before successful close; ESP_ERROR_CHECK failed: esp_err_t 0x107 (ESP_ERR_TIMEOUT) at 0x480087ea
- p4-dsi / present / diagnostic / DOUBLE_PARTIAL 90°：Run failed before successful close; ESP_ERROR_CHECK failed: esp_err_t 0x103 (ESP_ERR_INVALID_STATE) at 0x480087e6
- p4-dsi / present / diagnostic / TRIPLE_PARTIAL 90°：Run failed before successful close; ESP_ERROR_CHECK failed: esp_err_t 0x107 (ESP_ERR_TIMEOUT) at 0x480087e6
