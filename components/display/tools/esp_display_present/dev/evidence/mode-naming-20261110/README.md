# 内部模式命名收敛验证（2026-11-10）

基线 cf52571bd。内部 GRAM backend 改为 region，lcd_rgb_partition 改为 lcd_blit；不新增公开模式、不改输入合同和调度策略。参见 [模式架构说明](../../../docs/MODE_ARCHITECTURE.md)。

- 26个修改的 C/H 文件经明确的标识符与诊断名称替换、仓库格式化后，C token 与基线一致；公开头文件无差异。
- 最终源码的 ASan/UBSan + LVGL9.6.0 48项检查全部通过。
- 最终源码 S3 RGB、S31 RGB、P4 DSI 编译链接通过；各编译 manifest 中组件源文件哈希已逐一核对。
- 本轮改名没有再次烧录。前一提交的两板各96项交接/像素/任务名验证及原固件恢复凭据见 ../worker-rgb-facts-20261110；这些证据保留当时源码哈希，不冒充改名后的上板记录。
- 历史性能报告与 Kconfig 键仍保留 GRAM 名称，以免丢失报告上下文或既有 sdkconfig。
