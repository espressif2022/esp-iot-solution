# GRAM 后台提交首版证据

结论与适用范围见 [复测报告](../../GRAM_BACKGROUND_RESULTS_20261104.md)。最终交付对应 **`bounded`**，不是早期的全路径实验。

| 日志标签 | 含义 |
| --- | --- |
| baseline | 已提交基线 c260e7eb7，原版 IDF |
| candidate | 首轮全 GRAM 后台提交，sender 默认 5，出现回退 |
| priority | 同一诊断固件内的 sender 6／7 对照；临时全路径策略 |
| final | 曾尝试全路径＋正式优先级 7 配置，90° 仍回退，未采用 |
| bounded | 最终首版：只为同步内部 tile 开启新策略，sender 7；旋转／PSRAM 沿用原策略 |

`results.json` 按标签及优先级分组，保存全部三次样本和阶段计时；`comparison.csv` 只比较 baseline 与 bounded。不能将不同阶段或优先级的样本合并成最终均值。所有值为合成提交吞吐，并非 LVGL 或光学帧率。

`prototype-source.tar.gz` 与 `implementation.patch` 是最终源码及相对基线的实现差异；`baseline-source.tar.gz` 是基线组件。前者包含 Host 测试源码与入口。`priority-probe-source.tar.gz` 含诊断专用的全局优先级注入，配合 `perf-priority.c`；该注入没有进入工作区实现。

`projects/` 保存基线、最终原型和优先级诊断工程的 CMake 与 sdkconfig。使用原版 IDF 7b9cc1ac79f8、对应仓库依赖和 LVGL 9.5.0，将临时路径重定位到解包组件、生产者和 BSP 后可构建。`board/` 是测试使用的 QSPI BSP；其 co5300 依赖来自仓库板测 deps，运行版本为 2.2.0。

`firmware/` 保存各阶段测试 app。早期宽范围变体以日志、固件与优先级诊断源码供排查，不作为最终交付；最终可重建源码及配置对应 bounded。原始串口日志保留初始化和回调覆盖等告警。

`host-test.log`／`sanitize-test.log` 记录 36 项 suite；新增的四组 deferred 行为场景在 scheduler suite 内，默认 factory 实验开关关闭，原型场景显式使用内部 deferred 配置。ASan/UBSan 使用 detect_leaks=0。

`*-status.json`、`*-restore-verify*.log` 记录原设备镜像恢复后的整片校验。备份校验及基线恢复校验各遇到一次 USB I/O 中断，独立重试成功。设备备份和恢复前缀包含设备原内容，不归档。

`manifest.json` 包含源码指纹、设备备份指纹、版本与限制；所有文件字节校验见 `SHA256SUMS`。
