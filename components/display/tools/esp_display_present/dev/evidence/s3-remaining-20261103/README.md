# S3 剩余性能问题的实验记录

本目录所有候选均未进入生产代码，结论见 [调查报告](../../S3_REMAINING_INVESTIGATION_20261103.md)。

`results.json` 汇总可解析的三轮样本；`s3-bursts-perf.log` 在同次启动内包含 32 和 64 字节两组 GDMA 配置，需按 `BURST_BEGIN` 分组，未混算进普通三轮统计。

`s3-profile-perf.log` 的计时编号为：0 行数函数内部、1 core submit、2 backend region submit、3 pixel submit、4 executor dispatch、5 CPU execute、6 copy completion、7 copy progress 通知。各段嵌套，不能相加。`s3-v1profile-perf.log` 中 8 为整段 tile blit、10 为 memcpy 行循环、11 为目标缓存写回。计时包含被抢占的时间，不能直接解释成净 CPU 指令耗时。

`*-status.json` 与 `*-restored-verify.log` 记录设备恢复；备份含设备数据，不归档。`syncrows-sources/`、辅助快照和 diff 是实验源，不是可合入补丁；GDMA 原型还依赖实验 app 的突发配置变量。部分中间实验没有保留完整的可重建固件源快照，因此本目录用于排查与否定候选，不作为完整发布验收材料。
