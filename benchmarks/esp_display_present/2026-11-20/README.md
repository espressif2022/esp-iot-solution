# Present / adapter 完整矩阵 · 2026-11-20

本轮完整成绩 253/254 组，4048 条场景记录；独立同栈总内存 254 组（其中 253 组可匹配完整帧率成绩）。

| 板卡 | 完整成绩 | 计划 | 对齐内存 |
| --- | ---: | ---: | ---: |
| S3 RGB | 48 | 48 | 48 |
| S31 Korvo RGB | 48 | 48 | 48 |
| P4 MIPI DSI | 47 | 48 | 48 |
| BOX-3 SPI | 20 | 20 | 20 |
| mosaico QSPI TE | 45 | 45 | 45 |
| VoCat QSPI TE | 45 | 45 | 45 |

index.html 可直接本地打开。results.csv 为原始 FPS、非空绘制率与运行期最低空闲；scenes.csv 含各场景 render / flush 和像素量；memory-aligned.csv 含静态 SRAM、堆与打开后总占用；data.json 为完整网页数据。

本轮数据独立于历史报告。原始 LVGL FPS 为原统计；非空绘制率为 16 场景逐场景非空绘制次数 / 秒的算术平均，所有资源等待计入。都不等同于光学或逐帧确认显示帧率。单次完整运行，未做重复试验置信区间或画面目视验收。

条件：LVGL 9.5.0、15 ms 刷新、相同 DPI 与板级驱动；RGB / DSI 开启 XIP。BOX-3：40 MHz、事务上限 153600 B、队列 1；mosaico：40 MHz、事务上限 460800 B、队列 10、PSRAM DMA 直传开启；VoCat：80 MHz、默认事务上限（max_transfer_sz=0）、队列 10、PSRAM DMA 直传关闭。同一块板双方完全相同。SRAM 输入为 50 个逻辑行，PSRAM 输入为全屏容量。PSRAM 全屏容量仍采用 PARTIAL 渲染。adapter PARTIAL 原生仅一个输入 tile，因此不填 D2 adapter 对照。NONE 对照固定输入数量与内存域，Present D2 后台提交与 adapter 调用任务提交的调度不同。TE 为近似布局对照，不能当成相同资源配置。

总内存来自独立打开 / 关闭采样：两边均为 24 KiB 内部 SRAM 控制任务与 24 KiB 内部 SRAM LVGL 任务，优先级 6、不绑核，第一次刷新前取样。完整 benchmark 保持链接。SRAM Total Used = 链接器静态 SRAM + 总 INTERNAL|8BIT 堆 − 空闲 INTERNAL|8BIT 堆（堆可包含 RTC RAM；排除缓存/ROM 固定保留及 RTC 静态区）；PSRAM Total Used = 物理 PSRAM − 空闲 PSRAM。框架额外任务正常计入。Run Min Free 的局部监测从打开显示前开始，覆盖打开过程与完整 benchmark，单列且不与独立打开采样混算。

修复：独立 copy worker 在生产者会话重新绑定后继承新生产者优先级。OS 优先级修改由 copy worker 在下一次任务执行前完成；固定 backend override 保留。没有固定优先级 7，也没有逐帧动态切换。负向对照可在修复前代码检出该问题。

验证：冻结源码的 LVGL 9.5 Host 常规、性能监视器、ASan+UBSan、ThreadSanitizer 均 61/61。测试桩读取整幅共享画布造成的数据竞争已改为仅检查实际 src_area。冻结阶段额外 LVGL 9.2.2 的一次失败已定位为测试夹具未同步 hold 标记且遗漏部分迟到完成回调；发布分支修正夹具后，常规、ASan+UBSan、TSan、LVGL 9.2.2 均 63/63（新增两个 IDF 6.0 回调语义回归），9.2 公开旋转连续 30/30 通过。

源码在开测前冻结，SHA 见 source-freeze.json。ESP-IDF 使用干净的 6.2-dev 提交 7b9cc1ac79f8。本数据分支另带 IDF 6.0 DSI 回调兼容提交，覆盖 rev < 3.0 DMA EOF 和 rev >= 3.0 Bridge VSYNC 的安全退休受控模型；它是在真机矩阵冻结之后加入，未在本轮以 IDF 6.0 固件跑真机性能矩阵。提交 SHA 及验证范围见 publication-code.json。启动或采集异常已补跑，仍失败项见 validation.json 和网页异常区。缺少完整场景的尝试不算通过。

压缩包仅含本网页、JSON 与 CSV，不含工程源码、固件、串口日志或附件。原始证据保存在本机 /tmp/present-complete-20261120。未恢复旧固件。
