# 统一测试工程整理验证 — 2026-11-06

维护入口：[dev/README.md](README.md)，执行工具：[tools/run.py](tools/run.py)。本轮仅整理工程和编译验证，没有访问串口、烧录或恢复板子，没有生成新的 FPS 数据。

- Host sanitizer + LVGL + 工具检查：40/40通过。其中统一工具9项测试，覆盖矩阵、筛选、指标解析、错误与未完成状态、错误MAC拒绝、对比配置不一致拒绝、不支持组合写 -。
- 四板×present/adapter，共8个 benchmark 固件构建通过，均包含四角度配置。两种实现的板子、矩阵、IDF commit、LVGL benchmark源文件、关键sdkconfig指纹逐项一致。
- P4 pixels、S3 native、S31 te-lifecycle 迁移入口均编译通过。
- 仓库 pre-commit、astyle 和差异空白检查通过。
- 本次归档见 [evidence/unified-tests-20261106](evidence/unified-tests-20261106/validation.json)，包括构建日志、配置、矩阵与指纹，不含build或Flash备份。

| 板子 | present 计划组合 | adapter 同一计划 | 构建 |
|---|---:|---|---|
| S3 RGB | 7模式×4角度=28 | NONE非零角度和AUTO明确SKIP | 双方通过 |
| P4 DSI | 7模式×4角度=28 | 同上 | 双方通过 |
| BOX-3 SPI | 5模式×4角度=20 | 4个GRAM/0°，其余明确SKIP | 双方通过 |
| S31 QSPI TE | C1/C2×T1/T2×4角度=16 | C/T显式矩阵无同义配置，全SKIP | 双方编译通过；adapter不是TE运行通过 |

公共 benchmark 统一主程序、场景统计、清零、超时和日志；实现差异集中在 bridge_present/bridge_adapter。S31 的 TE 统计复用本轮实测版本，先 drain 再输出统计。board 以 MAC 配置，不保存固定端口。构建、配置和依赖默认在组件外，验证固件只用于后续明确选定板子的测试。

旧 examples 中应用示例和已存在的用户修改保留。历史 evidence 的源码快照保留原样用于追溯，不再作为新测试入口。legacy_compat、scanout_boundary 和根目录标准 IDF test_apps 的自动发现入口不移动，统一由总说明索引，避免破坏已有CI或隔离观测流程。

后续首次RGB板测发现统一配置遗漏已确认bounce声明、历史32KB指令cache/PSRAM XIP设置，现已补齐。对应运行与受控A/B见 [RGB NONE优化](evidence/rgb-none-profile-20261106/README.md)，本页保留整理当时的编译验证，不自动等同修正后全矩阵板测。
