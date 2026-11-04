# S3 FB IRAM 最终验收证据

结论与边界见 [复测报告](../../FB_IRAM_RESULTS_20261103.md)。

- `s3-stock-perf.log`／`s3-stockoff-perf.log`：最终同驱动 A/B；对应 `firmware/s3-stock-on`／`s3-stock-off`。两边使用真实 IDF 工作区的原版 esp_lcd，没有查询或退休补丁。唯一 sdkconfig 差别是 `ESP_DISPLAY_PRESENT_FB_IRAM`。
- `results.json`：每次测量的原始阶段计时、FPS 与三次平均；`sections.json`：段大小，完整 map 为各固件目录的 `link.map.gz`。
- `linkage-check.json`：抽取的符号地址；旧 overlay 的关闭选项样本显示 Flash 地址，最终启用样本显示 IRAM／DRAM 地址。
- `s3-traceiram-*`：独立插桩轨迹、分析与设备恢复；`diagnostic-source.tar.gz` 包含其隔离组件、驱动及记录器源码。插桩构建沿用只读能力查询和测量钩子，不用于性能均值。性能结论来自上面的原版驱动 A/B。
- `component-source.tar.gz`：本次最终组件的完整运行时源码、公开头和构建策略；`manifest.json`：源码指纹、IDF、工具链及限制。
- `producer/`：原始生产者及 BSP、项目 CMake 文件。复建时将其中工作区路径重定位到解包组件、对应仓库依赖和 LVGL 9.5.0；保留相应 sdkconfig。原版驱动测试确认 BSP 的 RGB bounce 配置，并启用既有 `PV2_LEGACY_RGB_BOUNCE_CONFIRMED` 声明。
- `s3-iram`／`s3-hotiram`／`s3-pipelineiram`／`s3-finaliram`：从整体放置到最终范围的中间对照，沿用之前只读能力查询 overlay。`prototype-linker/` 保留放置范围。
- `*-status.json`／`*-restored-verify.log`：恢复原镜像后的完整 16 MiB 校验。设备备份含原设备内容，不归档。
- Host 36 项测试及 P4 构建日志已保留。P4／S31 本轮没有重新做实板吞吐测试。

该证据集证明指定配置的提交吞吐恢复及有限轨迹内的所有权检查通过，不是光学帧率、LVGL benchmark 或穷尽全部硬件异常的证明。所有文件校验见 `SHA256SUMS`。
