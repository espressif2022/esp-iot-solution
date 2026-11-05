# S3 RGB：原始 esp_lvgl_adapter 与修复后 present 的 LVGL benchmark 对照（2026-11-05）

同一块 ESP32-S3 R16 / GC9503 480×480 RGB，MAC 90:e5:b1:d0:a9:d8。present 先测，恢复原镜像后再测原始 adapter；不是交替顺序或多轮统计 A/B。
原始 adapter 为仓库中未修改的 0.7.4（无 HPx80 同步补丁）；present 为本轮 scanout 修复版本。两边使用相同 IDF、LVGL 9.6.0 及逐字节相同的 lv_demo_benchmark.c，CPU/PSRAM/缓存/LVGL/编译优化配置逐项匹配。RGB565，15ms 刷新周期，50 行单 tile，2 个 LVGL 绘制单元，RGB bounce 10 行，实际 panel 都分配 3 个 FB。应用分别为 lvgl_common_demo 和 lvgl_present_benchmark，集成任务/缓冲流程是被比较的实现差异。

## LVGL 汇总 FPS：原始 adapter → present

| 模式 | 0° | 90° | 180° | 270° |
|---|---:|---:|---:|---:|
| NONE | 51 → 25 | 不支持 → 15 | 不支持 → 14 | 不支持 → 15 |
| DOUBLE_FULL | 26 → 27 | 15 → 17 | 16 → 16 | 15 → 16 |
| TRIPLE_FULL | 37 → 37 | 15 → 19 | 16 → 19 | 15 → 19 |
| DOUBLE_DIRECT | 42 → 21 | 26 → 13 | 29 → 13 | 24 → 13 |
| DOUBLE_PARTIAL | 42 → 45 | 36 → 41 | 42 → 44 | 37 → 41 |
| TRIPLE_PARTIAL | 47 → 48 | 43 → 45 | 46 → 47 | 42 → 45 |

present 的 AUTO 另测为 48 / 45 / 48 / 45 FPS，原始 adapter 没有直接对应的 AUTO 模式，未强行配对。

原始 adapter：有效完成 21 组、跳过 3 组，矩阵完成标记 True；每组完整 16 场景。present 28/28 组和 448 个场景均完成。

## 如何解读

LVGL FPS 是各有效场景统计的平均，单轮的小幅差异不能作为稳定性能增益保证。原始 adapter 日志的 frames=0 / transport_fps=0.0 是未实现计数的占位值，不是零吞吐；本报告不比较该列。render/flush 的归因随两种集成方式变化，不能仅凭某一列单独判断像素执行耗时。
NONE / 0° 存在明显差距：原始 adapter 51 FPS，present 25 FPS。源码上 present 单 FB active-write 仍经过 scanout exposure 完成门控，默认等待两次事件；原始 NONE 使用不同的刷新完成路径。这个时序差异值得单独做受控实验，本轮未修改 NONE 策略来追平数据。
DIRECT 的四个角度存在明显退化：原始 42 / 26 / 29 / 24，present 21 / 13 / 13 / 13 FPS，约下降 46%–55%。FULL 基本持平或本轮略高，PARTIAL 尚未出现这种退化。未支持的组合仅记 SKIP，不赋零分，也不修改原始 adapter 以使其通过。
从代码看，present 的 DIRECT 在继承不同 baseline 时可能全帧 prefill；旋转时还会全帧转换。LVGL 接入传递的损伤信息和上一帧继承策略应优先检查，实际各段贡献尚未通过插桩量化，本轮不把候选原因写成已证实根因。

## 画面与安全边界

本轮没有逐项完整目视或光学采样，原始 adapter 的性能不证明其缓冲同步安全。此前用户在 present 的 NONE 90°→180° 附近报告过红绿蓝场景撕裂，NONE 允许写扫描缓冲，记录保留在上一轮 optical-observations.json。双/三缓冲是否彻底无撕裂，仍需独立目视和退休地址/时序验证。
原始 adapter 没有本次 post-submit ISR join 修复，不能为了 FPS 将已确认的安全要求退回原始协议。

## 证据与设备恢复

comparison.csv 保存每个模式/角度的 FPS 差值和 render/flush；adapter-scenes.csv 保存原始 adapter 分场景数据；s3-rgb-live.log 为本轮原始串口；manifest.json 保存源码/配置/固件指纹。原始 adapter 仅由不支持的 NONE 旋转产生预期错误日志，其余异常单独列在 validation.json。
对照测试前再次验证整片 16MiB 与上一轮原始备份一致。测试结束后恢复本次改写范围，并独立核验完整 Flash，已恢复本次改写的首 1MiB，并独立核验完整 16MiB 与原始备份 digest 一致；状态见 restore-status.json。备份含设备内容，只留本地，不随报告归档。
present 的原始测试详见同级 rgb-scanout-benchmark-20261105 证据目录。
