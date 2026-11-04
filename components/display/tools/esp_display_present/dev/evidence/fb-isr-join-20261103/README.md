# FB ISR join 复测证据

结论、测试条件和限制见 [结果报告](../../FB_ISR_JOIN_RESULTS_20261103.md)。

- `p4-perf.log`、`s3-perf.log`：生产补丁的四模式、三轮未插桩性能日志。
- `*-trace-perf.log`、`*-trace-analysis.json`：独立的双 PARTIAL／DIRECT 诊断与检查器结果。S3 PARTIAL 有一次 EOF 不足 restart，不能作为完整安全通过证据。
- `sources/`：上述主测试使用的关键组件源码；`manifest.json` 记录源码与实际固件摘要。
- `diagnostic-hooks.diff`：相对生产补丁与只读能力查询 IDF 副本的测试插桩；`idf-readonly-capability.diff` 为沿用的能力查询补丁。没有 tagged-retirement 驱动改动。
- `perf.c`、`perf_trace.c`、`boundary_trace.*`、`board_*.c`、`sdkconfig-*` 与 CMake 文件：测试生产者、记录器和构建配置。配置保留本机路径，复用时需要调整。
- `experiment-*.diff`、`s3-*-perf.log`：被排除的 CPU copy 对照，只在隔离副本运行，不属于正式补丁。
- `*-verify*.log`、`*-status.json`：恢复与校验记录。P4 曾发生 USB 重新枚举的连接失败，以最终成功状态及独立整片校验日志为准；备份二进制包含设备数据，不归档。

运行 `python3 analyze_trace.py p4-trace-perf.log s3-trace-perf.log` 可重算诊断统计；`sha256sum -c SHA256SUMS` 核验本目录附件。记录器的时间戳是软件标记，不代表外部总线测量；分析器未报告问题不等于完整竞态或光学验证通过。
