# RGB / MIPI 当前源码完整有效矩阵

[网页](comparison.html) · [当前表 JSON](comparison.json) · [CSV](comparison.csv) · [原始数据与 manifest](matrix-data.json) · [诊断说明](findings.md)

仅展示当前源码有效结果，不混入已失效旧版的 FULL 占位画布数据。范围：S3 RGB、S31 Korvo RGB、P4 DSI；六模式四角度。全部 XIP ON；S3 十行 bounce，S31 非 bounce；NONE 排除，adapter 不支持 AUTO 记横杠。

用户要求已测项不要重复：复用生产源码与公共配置一致的 present 24 项（每板 8 项），只补缺少的 present 48 项（每板连续完整 16 项）。adapter 的实际对齐源码、LVGL 场景、时钟/XIP/缓存配置、默认值、case 表和 ELF 静态 SRAM 与当前重新构建结果相同，因此复用已有 60 项。原始 manifest 保留实际采集来源；没有将复用改称本轮重跑。

正式覆盖 132 个组合：128 项通过、4 项失败。只有通过项的完整 16 场景与成功关闭才计入分数。S3 为 8+16 两个完整批次；S31 复位中断后复用完整的 12 项，仅补余下 4 项；P4 按 case-ledger.json 逐项记录已完成与失败，复位后只采集未覆盖项。未将这些批次称为一次连续全通过的 24 项跑测。四阶段内存保留各批次实际记录，不扣除或归一化首次 LVGL 全局初始化。ELF 静态 SRAM 和动态堆分列，它们属于整套固件，不是组件独占。

交接诊断独立于正式成绩：present 72 个组合新采集，adapter 60 项核对一致后复用；共 132 个组合，其中 130 项通过、2 项未完成。正式与诊断累计 4128 个有效完整场景，24 个不支持条目；失败尝试不计有效场景或 FPS。交接和供数只检测记录，不修正、不 assert，不注入故障；不能由零命中推导所有情况下无撕裂。失败尝试单独保存，不用其计分补表。

FULL 和原生 0° DIRECT 不再分配绑定层全屏占位画布。软件旋转 FULL 保留实际逻辑画布；旋转 DIRECT 保留实际绘制画布。S31 FULL 180°/270° 的打开 PSRAM 增量为约 751 KiB，不再采用失效版本约 1503 KiB 的数据。AUTO 依据实际池选三缓冲/双缓冲 PARTIAL，单 FB 拒绝。

正式轮使用干净 IDF；诊断轮只使用隔离 esp_lcd 副本，实际组件路径与观察覆盖有验证。clean-source.tar.gz / diagnostic-source.tar.gz 保存实际源码，consumer-observer.patch 与 driver-observer 保存只记录的改动。缺项过滤由 missing_runner.py 调用原 case planner 后排除已测 8 项，并记录实际生成 case 表。不改生产代码、IDF，不恢复原固件。没有 build、依赖缓存、固件镜像或 flash 备份目录。

validation.json 核对完整覆盖、原始记录、配置/容量、干净 IDF、观察点控制流以及源码不变性；reuse-validation.json 说明复用依据。网页数值与布局由浏览器另行校验，结果在 browser-validation.json。

`matrix-data.json` 的 complete / coverage_complete 表示各组合已有明确采集结果，不表示全部通过；全通过标志见 current_round.all_passed，失败由 status 和 summary.errors 明确记录。

不同采集批次的 case 表大小及 XIP 影像可能造成亚 KiB 的 PSRAM 基线差异。保留原始剩余值；打开增量以同一项的 before/after 相减，不把基线差异当成帧缓冲开销。诊断统计仅包含成功输出报告的组合，未完成项不当作零异常；S3 的既有重启是驱动扫描重启，不是芯片复位。
