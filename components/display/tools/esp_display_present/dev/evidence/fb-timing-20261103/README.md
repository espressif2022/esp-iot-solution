# 本轮 FB 性能与边界记录

结论、条件和限制见上级 [FB_TIMING_V1_V2_20261103.md](../../FB_TIMING_V1_V2_20261103.md)。

- `*-run.log`：四次无插桩性能测试和四次插桩测试的原始串口输出；包含 S3 诊断 restart，不能用作完整安全通过声明。
- `perf.c` / `perf_trace.c` / `board*`：测试源码；`boundary_trace.*`：环形记录器。
- `diagnostic-hooks.diff`：相对 V1、V2、IDF 三个基线的最终插桩改动。将 `boundary_trace.h` 放入三套组件 include 后应用相应部分；此补丁不用于生产。
- `idf-readonly-capability.diff`：测试所用 IDF 基线的只读能力查询隔离补丁；`v2-worktree-source.diff`：V2 工作区相对于基线提交的源码差异。
- `trace-analysis.json`、`analyze_trace.py` 和 `test_analyze_trace.py`：记录解析、分析与最小回归。运行 `python test_analyze_trace.py`，或向分析脚本传入原始插桩日志。
- `sdkconfig-*` 和 `*-project-*`／`*-main-*`：编译配置及当时 CMake 文件，里面保留了本机绝对路径，复用时需要根据本机路径调整，不能直接作为独立工程执行。
- `manifest.json`、`*-firmware-sha256.json`、`SHA256SUMS`：版本、最终插桩文件与实测二进制摘要以及本目录校验值。固件文件本身仅存于临时目录，设备备份不在此目录。

校验：`sha256sum -c SHA256SUMS`。ISR 记录会扰动时序；具体证据限制见报告，尤其不能把指针匹配当作每个提交代次的证明。
