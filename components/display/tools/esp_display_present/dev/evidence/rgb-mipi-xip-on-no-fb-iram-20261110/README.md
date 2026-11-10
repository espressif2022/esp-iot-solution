# 移除 S3 FB_IRAM 后的对比汇总

[网页](comparison.html) · [CSV](comparison.csv) · [完整数据](matrix-data.json) · [检测结果](findings.md)

本次新测 S3 present 的 6 模式 × 4 角度：24 项干净 IDF 帧率/内存、24 项隔离驱动诊断，共 768 个完整场景。XIP ON，bounce 10 行，32 KiB 指令缓存。基础 IDF 干净，不恢复原固件。每项从打开到完整 16 场景结束并关闭成功。

adapter 及 S31/P4 沿用 rgb-mipi-xip-on-four-angle-20261110 的完整四角度记录；本次改动不影响这些构建。对比表合计 132 正式 / 132 诊断，有 108 / 108 项沿用，不称全部重新跑测。原始历史数据不改，来源和实际 ELF SHA 见 JSON，正式与诊断不混成绩。

## SRAM 更正

ELF IRAM 与普通 DRAM 静态段单列；排除 dummy 别名，避免重复计算。RTC / P4 SPM 不计入普通 SRAM 段。段小计不等于芯片物理总量，缓存、保留区、对齐还影响堆容量。统计工具、readelf 输出、实际链接组件、日志及配置均归档。

网页单位为 KiB，CSV/JSON 内存保留原始 bytes；CSV 明列正式与诊断状态，不支持的 adapter AUTO 为 SKIP。四个剩余值保留，剩余越多越好。可切换堆已用：实际堆容量 − 剩余，运行期取本项局部峰值，越少越好；静态段不在堆已用内。箭头数值始终 present−adapter，颜色按指标优劣判断。RAM 与 PSRAM 的最低点不保证同时发生。

这是整套测试固件的账目，包含驱动、LVGL、任务和应用，不是组件独占占用。adapter 的 LVGL 任务在 bench_init 创建，present 首次打开才初始化 LVGL；不能直接用每项打开增量比较总开销。两者共用测试主任务，但 present 由主任务 pump、adapter 自有 LVGL 任务，不能把任务差异误称为纯 core 占用。关闭仅注销本项显示对象，共用驱动池、adapter 任务和 LVGL 全局资源仍可保留，并非全系统释放。

最终六个干净 ELF 未链接 JPEG 解码组件/入口，不扣除假定的 20 KiB。声明可选依赖不等于最终镜像包含库。memory-accounting.json 保存实际 ELF 哈希、段大小、SUITE 初始化前后基线及检查结果。

## FULL 缓冲核对

当前测试双方在 bench_init 都固定创建 3 张驱动 FB，已计入打开前；不是每个模式按需创建。present DOUBLE_FULL 使用其中 2 张，第三张仍预留；TRIPLE_FULL 使用 3 张。

present 的 LVGL 绑定层另外无条件创建一张全屏 draw 回退画布，供 detach/错误处理等路径使用。正常 RGB565 FULL 零拷贝刷新借用 presenter 的 lease，并非每帧都先画此画布再 memcpy。该画布真实占用 PSRAM，当前并未消除；所以模式名的 2/3 指扫描 FB，不是所有分配的全屏像素块总数。

有软件旋转（90/180/270 度）或字节序转换时，FB backend 还创建 1 张共享逻辑画布用于转换，区别于前述绑定层回退画布。当前 FULL 分配核对：

| 场景 | 驱动池已分配 | presenter 使用的原生 FB | backend 额外逻辑画布 | LVGL 绑定额外回退画布 |
| --- | ---: | ---: | ---: | ---: |
| DOUBLE_FULL 0 度 | 3 | 2（来自左列） | 0 | 1 |
| TRIPLE_FULL 0 度 | 3 | 3（来自左列） | 0 | 1 |
| DOUBLE_FULL 90/180/270 度 | 3 | 2（来自左列） | 1 | 1 |
| TRIPLE_FULL 90/180/270 度 | 3 | 3（来自左列） | 1 | 1 |

原生 FB 列是驱动池子集，不能两列相加。三板每张 RGB565 全屏分别为 S3 450 KiB、S31 750 KiB、P4 1200 KiB；不含管理结构和对齐。adapter 的旋转路径还会将已有 panel FB 用作绘图源，不能只看 double/triple 名称就断言双方图像池数量相同。这里解释现有分配，本次未同时改动缓冲生命周期，避免混淆 IRAM 前后帧率对照。

## 复现与归档

collect_memory_accounting.py 读取实际 build/present_benchmark.elf、project_description.json、live.log，随后 render_comparison.py 从汇总 JSON 生成网页。源码补丁与新测源码快照、原对照源码快照、隔离 observer 均归档。不含 build、固件、依赖缓存和 flash 备份。硬件性能只用于已测配置，零交接命中不能保证所有视觉现象被覆盖。
