# 四角度 XIP ON 交接诊断结果

本轮为全新完整四角度运行；不合并上一轮0°或XIP OFF数据。地址命中按检查点累计，同次提前交接可能对应多条记录，不代表独立撕裂次数。kind1交还、kind2取得可写/渲染开始、kind3确认。每项仅保存前16条样本，累计计数不截断。

[网页](comparison.html) · [原始样本CSV](handoff-events.csv)

## s3-rgb

检查实际用于bounce取数的PSRAM framebuffer地址。

- present：本轮已覆盖检查点未检出地址命中；未知源 0 次。
  RUN供数不足 150/100630 帧；既有自动重启 150 次。
- adapter：DOUBLE_FULL 0°: 2次；DOUBLE_DIRECT 0°: 2次；未知源 0 次。
  RUN供数不足 96/83827 帧；既有自动重启 96 次。

## s31-rgb

检查AXI预取描述符所属framebuffer；属于保守的DMA借用地址核对，不等于逐像素扫描位置。

- present：本轮已覆盖检查点未检出地址命中；未知源 2 次。
  RUN硬件underrun中断 0 次。
- adapter：本轮已覆盖检查点未检出地址命中；未知源 0 次。
  RUN硬件underrun中断 0 次。

## p4-dsi

检查驱动实际装载到DMA的帧地址独立快照；记录LLP作上下文，不等于逐像素扫描位置。

- present：本轮已覆盖检查点未检出地址命中；未知源 0 次。
  RUN硬件underrun中断 0 次。
- adapter：DOUBLE_FULL 90°: 1次；TRIPLE_FULL 0°: 1次；TRIPLE_FULL 90°: 1次；DOUBLE_DIRECT 0°: 2次；TRIPLE_PARTIAL 0°: 1次；未知源 0 次。
  RUN硬件underrun中断 0 次。

## 原始地址样本

| 板卡 | 实现 | 模式 | 角度 | µs | kind | 交付地址 | 实际源/装载快照 | 请求目标 | request_seq | event_seq |
| --- | --- | --- | ---: | ---: | ---: | --- | --- | --- | ---: | ---: |
| s3-rgb | adapter | DOUBLE_FULL | 0 | 61749534 | 3 | 0x3c151400 | 0x3c0e0bc0 | 0x3c151400 | 1467 | 3608 |
| s3-rgb | adapter | DOUBLE_FULL | 0 | 61752897 | 2 | 0x3c0e0bc0 | 0x3c0e0bc0 | 0x3c151400 | 1467 | 3608 |
| s3-rgb | adapter | DOUBLE_DIRECT | 0 | 606304703 | 3 | 0x3c0e0bc0 | 0x3c151400 | 0x3c0e0bc0 | 1782 | 2757 |
| s3-rgb | adapter | DOUBLE_DIRECT | 0 | 606306022 | 2 | 0x3c151400 | 0x3c151400 | 0x3c0e0bc0 | 1782 | 2757 |
| p4-dsi | adapter | DOUBLE_FULL | 90 | 69644031 | 2 | 0x480f0a40 | 0x480f0a40 | 0x480f0a40 | 0 | 1 |
| p4-dsi | adapter | TRIPLE_FULL | 0 | 278579512 | 2 | 0x4821ca80 | 0x4821ca80 | 0x4821ca80 | 0 | 1 |
| p4-dsi | adapter | TRIPLE_FULL | 90 | 347860496 | 2 | 0x480f0a40 | 0x480f0a40 | 0x480f0a40 | 0 | 1 |
| p4-dsi | adapter | DOUBLE_DIRECT | 0 | 611418801 | 3 | 0x480f0a40 | 0x4821ca80 | 0x480f0a40 | 1854 | 3302 |
| p4-dsi | adapter | DOUBLE_DIRECT | 0 | 611420829 | 2 | 0x4821ca80 | 0x4821ca80 | 0x480f0a40 | 1854 | 3302 |
| p4-dsi | adapter | TRIPLE_PARTIAL | 0 | 1147894726 | 1 | 0x480f0a40 | 0x480f0a40 | 0x4821ca80 | 1888 | 2175 |

## 本轮样本分类

S3 adapter DOUBLE_FULL 0°和DOUBLE_DIRECT 0°各出现一组配对：确认的新地址与当时实际bounce源不同，随后LVGL绘制的旧FB仍等于实际取数源。每组两条检查点记录，共两组，不能算四次独立撕裂。

P4 adapter的6条记录不能全部归为初始化：DOUBLE_FULL 90°、TRIPLE_FULL 0°/90°三条request_seq=0，发生在本模式首次提交前；DOUBLE_DIRECT 0°在request_seq=1854出现确认/随后可写两条配对；TRIPLE_PARTIAL 0°在request_seq=1888交还的地址仍等于已编程DMA帧快照。后两组属于运行期地址交接风险。P4证据口径仍是已装载帧快照，不宣称取得逐像素扫描证明。

S31 present的unknown分别位于TRIPLE_PARTIAL 180°和AUTO 270°，各1次。它们未能稳定映射源地址，不能当作正常或已确认冲突。

S3供数不足累计present150/100630帧、adapter96/83827帧；两边覆盖模式数量不同（present24项、adapter20项），请按网页同模式同角度比较，不直接用两个累计次数判断优劣。均包含RUN结束统计日志窗口，且只来自独立诊断轮。

## 解释限制

S3实际bounce源、S31预取描述符、P4实际装载帧快照分开解释；后两者不是逐像素扫描证据。request_seq=0的样本发生在本模式首次提交之前，应核对共享panel重新打开时的初始front基线。仅记录，没有修复、assert或故障注入。零命中不能证明任意CPU/DMA写入安全；未知单列，不当作正常。前一轮0°记录保留在相邻rgb-mipi-xip-on-20261110历史目录，不混本轮计数。
