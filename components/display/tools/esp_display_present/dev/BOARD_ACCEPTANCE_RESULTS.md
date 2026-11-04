# 交付候选三板验收（2026-11-03）

以下是当时交付候选的历史记录，不能直接当作后续提交的验证；本轮结果见 [当前验证](VALIDATION.md)。

本文记录此次候选的设备、固件和串口证据；用户随后反馈三块板的测试画面均正常，P4 的 GSP／LVGL 交接界面也正常；以下日志与目视结论分别记录。测试前按 MAC 确认设备、全量备份并通过 digest 校验，测试后写回对应完整备份并再次验证 digest 一致。三块板 Secure Boot 与 Flash Encryption 均为 disabled。备份为用户数据，保存在权限 0700 的 `/tmp/present-final-board-backups/`，不纳入仓库；临时目录可能被系统清理。

| 板 | 设备身份 | 测试证据 | 恢复 |
| --- | --- | --- | --- |
| S3 RGB 480×480 | 90:e5:b1:d0:a9:d8，USB0 | 单次连续启动两轮；5 模式每轮 48 帧，逐像素检查及清理；详见 [日志](evidence/acceptance-20261103/s3.log) | 全量备份写回、独立 digest 通过 |
| P4 MIPI 1024×600 | e8:f6:0a:e7:a7:8d，ACM1 | 5 模式两轮，32 执行器用例；GSP 1.5.1/LVGL 交接 9 次、错误 0；详见 [FB 日志](evidence/acceptance-20261103/p4.log)与[交接日志](evidence/acceptance-20261103/p4-handoff.log) | 全量备份写回、独立 digest 通过 |
| S31 QSPI/TE 480×480 | 30:ed:a0:f4:0b:34，ACM2 | 4 组 compose/tile 矩阵，GRAM/TE 异步旋转，TE ROI 55 轮；详见 [日志](evidence/acceptance-20261103/s31.log) | 全量备份写回；首次独立校验 USB 重枚举中断，重试 digest 通过 |

P4/S3 的 runner 覆盖 DOUBLE/TRIPLE FULL、DOUBLE DIRECT、DOUBLE/TRIPLE PARTIAL；**NONE 单 FB 不在本轮覆盖**。S3 两轮来自同一次连续捕获；此前另一段捕获在串口重新打开时观察到 POWERON 复位，具体原因未定位；该段没有与本轮拼接。S3 测试反复报告 GPIO3 不可用的驱动 warning；串口逐像素回归正常，用户反馈 S3 画面正常；P4 handoff 的帧数/错误日志之外，用户也确认交接界面正常。

S31 ROI 窗口依据测量的 TE 高脉冲做诊断；full dynamic phase 与帧/回调检查不证明面板光学相位或无撕裂。IO 的 callback 替换和已安装 ISR service 日志出现在 adapter 重建阶段，传输计数和排空结果见原日志。候选使用本地 ESP-IDF 6.2 开发版，含 RGB 能力查询补丁；不代表未打补丁 SDK 或无 bounce RGB 已验收。

固件和依赖哈希见[候选清单](evidence/acceptance-20261103/candidate-manifest.json)。P4 GSP/LVGL handoff 使用缓存 GSP 1.5.1 P4 archive（SHA256 `5ab508eda956a05966ddbe16b186a367e2f19e90dc5fbce0cc1b668eb8a78cab`），16 MB 配置；app SHA256 `4549939b381b05b83994b05996eff738b4b6ab03877d6d5d8d3e2d50be2ecd39`。没有执行发布、Registry 下载验证或外部 ABI 验收。

## 用户目视反馈

- P4 MIPI：彩条和移动方块画面正常；GSP／LVGL 交接界面正常。
- S31 QSPI／TE：测试画面正常。
- S3 RGB：测试画面正常。

此反馈与串口结果相互补充，不等于测量了光学撕裂或覆盖了 NONE 单 FB。
