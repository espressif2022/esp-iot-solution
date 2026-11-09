# S31 RGB 全面定位：把访问路径、交接成本和统计口径分开

生产源码未修改。只对 S31 Korvo 800×480 做新硬件诊断，其他四板做现有记录及配置审计；不能将本专项当作全模式重新验收。

## 已确认的问题

1. **此前“双方开 XIP 后完全收敛”过度概括。** 相等的是 16 场景 FPS 的整数算术均值，场景差异可以相互抵消。综合成绩不能代表每个场景或实际送屏帧率相同。
2. **同样 50 行不代表同容量。** 原 adapter 按物理宽 800 分配 80000B；present 的 90° 按逻辑宽 480 分配 48000B。原对比混入了分块大小、flush 次数和运行内存差异。本轮统一 0° 80000B、90° 48000B，双方始终只有一个输入 tile；LVGL 软件绘制线程仍同为 2 个。
3. **LVGL FPS 包含空刷新。** sysmon 在 REFR_READY 增加 refr_cnt，未要求出现 RENDER_START。15ms 刷新周期的统计上限为 66；这不是面板每秒扫描的新帧数。本轮分别统计刷新、非空绘制和成功 panel draw_bitmap 提交，后者也不等于实际扫描完成。
4. **原 CPU 100% 不可信。** 两边 ELF 都保留 lv_os_get_idle_percent，却没有 lv_freertos_task_switch_in/out；默认 idle 计数没有接入任务切换。默认 idle 返回 0 导致 CPU 显示 100%。本轮改用 FreeRTOS 任务累计运行时间审计，未修改原始成绩。
5. **借用缓冲后仍分配保底像素存储。** present_lvgl.c:create_binding 无条件 lv_draw_buf_create，pooled PARTIAL 用它作保底，FULL/DIRECT 也分配整屏。S31 整屏 RGB565=768000B=750KiB。原 0° FULL/DIRECT 打开增量约 753KiB，对照 adapter 约 3–6KiB；90° present 约 1503KiB，还包含独立逻辑画布。此处有明确的内存优化空间，不能用 XIP 静态占用解释。
6. **单 tile 的异步管线有交接成本。** present 的 begin/acquire/submit/commit、像素 worker、后台 sender 和完成通知维护明确的所有权；adapter 的 PARTIAL 大多在 UI flush 内同步调用复制/旋转。一个 tile 在复制完成前不能被下一段绘制复用，因此异步接口本身不保证多段绘制与复制重叠。任务统计将两个 present_service 合计，不把同名任务擅自归因到单个角色。adapter 没有这两个专用 worker，其复制成本计入 UI 任务，不能把缺少 worker 误读成零成本。

## XIP 为什么影响这么大

原 S31 使用 80MHz DIO Flash、200MHz Octal PSRAM、16KiB I-cache、64KiB D-cache；S3 原测试则已经开启 XIP 且 I-cache 为 32KiB，两板不是相同的取指条件。S3 没有 S31 的 DMA2D/PPA；本 S31 日志确认 0° 走 DMA2D、90° 走 PPA，S3 的对应复制/旋转主要走 CPU。所以同样接 RGB，不代表像素处理路径和每块任务的启动成本相同。

XIP 将代码和只读数据从 Flash 移入 PSRAM，改变缓存未命中后的服务路径，并未消除 present 的状态机、锁、复制或任务切换。两个实现的 LVGL 可重定位 .text 字节一致，但最终地址、热路径组合和调度交错不同；adapter 的 rotate_copy_strided_region 在 IRAM，present 的部分交接函数仍在外部可执行区。LVGL 的 RGB565 图像混合函数单个就有 9728B。不能仅凭这些静态事实断定具体哪一组函数发生 I-cache 冲突。

本轮采集硬件“miss”原始寄存器时，按官方语义解释为**缺失访问期间的停顿事件**，不是缺失访问次数，更不能用 miss/(hit+miss) 算 miss 率。用每个非空绘制帧的停顿量作相对对照；I/D 两条路径分别列出。尚未采集 line_fills，不能进一步分解“未命中更多”和“每次缺失更慢”，也未将代码取指与字体/图片 rodata 的 XIP 贡献单独隔离。

原不带计时四项（双0/90、三0/90）为 present 17/17/24/23、adapter 19/24/27/29；同开 XIP 为双方 37/36/40/40。这证明该访问路径对这组成绩影响大，不证明所有模式、场景都等价。之前 burst32→128 和重复 cache 维护精简的无计时复核仍为 present 17/17/24/23，未追回差距，不再把它们当作主因。

## 已核对但不应误判为主因的项

- **不是整条显示链都更慢。** 同开XIP、同容量后，移动壁纸0°双方约42.1次有效绘制/秒，90°双方约21.08次/秒。该全屏场景已经基本一致；多标签的剩余差距应单独看小块请求路径。
- **不是“单tile”就意味着只有一份像素分配。** 本轮单tile限定的是输入池的活动tile数量；binding保底存储、旋转逻辑画布和panel framebuffer属于不同角色，必须分别记账。
- **不把stride自动标志当作PARTIAL根因。** 两边设置LVGL draw buffer的方式不同，但本版本PARTIAL绘制仍按实际子区域重塑stride；不能仅凭stride_is_auto标志不同断言固定整屏stride错误。
- **不通过放宽退役时序提速。** 本轮没有修改RGB bounce/VSYNC通知和framebuffer退役契约。串口性能记录也没有重新证明目视防撕裂；本次结论不支持提前释放扫描缓冲。
- **burst/cache维护不是这轮主要增益。** 此前单项及组合实验没有追回差距；在没有新证据前不继续围绕同一微调重复试验。


## 本轮屏幕文字场景（固定全屏像素工作量）

| XIP | 角度 | 实现 | 有效绘制/s | 软件绘制CPU ms/帧 | I-bus完成访问M/帧 | I-bus停顿M/帧 |
|---|---:|---|---:|---:|---:|---:|
| off | 0 | present | 6.86 | 192.17 | 23.06 | 40.55 |
| off | 0 | adapter | 9.73 | 104.76 | 22.77 | 11.24 |
| off | 90 | present | 5.32 | 227.71 | 33.10 | 44.03 |
| off | 90 | adapter | 7.39 | 150.80 | 33.16 | 15.14 |
| on | 0 | present | 13.23 | 103.40 | 22.72 | 5.65 |
| on | 0 | adapter | 13.70 | 94.61 | 23.15 | 1.71 |
| on | 90 | present | 9.20 | 143.47 | 32.77 | 6.38 |
| on | 90 | adapter | 9.92 | 134.14 | 32.64 | 2.28 |

本场景每个有效帧都是384000像素。I-bus计数为整机两条请求总线的窗口累计，包含框架和其他任务，不是仅LVGL指令数；停顿事件仅作相对指标，不换算绝对阻塞毫秒。CPU累计包括任务被记账的缓存停顿及部分中断开销。


## 同开 XIP 后仍落后的多标签场景

| 角度 | 实现 | 有效绘制/s | flush次数/帧 | 像素/帧 | UI＋专用worker CPU ms/帧 | SW CPU ms/帧 |
|---:|---|---:|---:|---:|---:|---:|
| 0 | present | 31.10 | 24.23 | 100026 | 25.39 | 8.59 |
| 0 | adapter | 42.16 | 24.17 | 98983 | 14.12 | 5.84 |
| 90 | present | 24.57 | 22.29 | 91347 | 26.23 | 6.49 |
| 90 | adapter | 42.16 | 22.15 | 89703 | 12.47 | 5.14 |

90°两边每帧像素量仅差约2%，present的UI及专用工作任务累计时间仍更高。这部分覆盖交接、状态维护、复制驱动及其缓存停顿，不能全部算作通知或锁本身的开销。原无诊断、同容量90°标签场景也存在present 29 vs adapter 41的LVGL分差；因此剩余问题并非只由本轮探针产生，但本轮有效绘制率不能替代原生产分数。

## 应优先处理什么

- 先修测试口径：同时展示非空绘制/送屏提交，按实际字节统一输入 tile 容量，真实 CPU 统计单列；保留原始 benchmark 分数便于追溯。
- 解耦 LVGL 保底描述符和整块像素存储。优先削减 FULL/DIRECT 的整屏重复分配；必须证明 detach、取消、销毁、旋转期间不会写到正在扫描的缓冲，不能直接删掉保底或指向 scanout。
- 再针对单 tile 交接路径优化。先用每帧 worker CPU、等待和停顿量找到耗时段，再考虑收敛通知/短任务的派发，以及在实测支持的阈值下让很小的复制请求使用 CPU。不能未经验证就更换所有模式的执行器。保持 framebuffer 退役契约，不以提前 flush_ready 或提前释放缓冲换成绩。
- XIP 是可选的性能/内存取舍。此前测得 present 约消耗 925.50KiB PSRAM 堆容量及 10.44KiB 内部堆容量；内部差值来自 IRAM text、DRAM data/BSS 和对齐，是静态链接布局开销，不是一个 10KiB 显示缓冲。
- FULL/DIRECT、三缓冲、180°/270°只能引用已有测试，不能把这次 DOUBLE_PARTIAL 两角度结果外推。新优化再以对应模式的硬件复测验收。

## 本轮诊断方法及边界

干净 IDF 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe；生产源码与 HEAD 535d753dc 的 99 个 src/include 文件一致。诊断只在隔离副本加入 benchmark 场景入口、事件/任务/缓存计数和错误定位。两边都启用相同 FreeRTOS 运行时间统计，保持单输入 tile、同容量、同绘制线程数和共享 SDK 配置；组件独有 Kconfig 单列于 sdkconfig-pair-audit.json。

每个固件跑完整 16 场景，0°/90°连续运行；统计窗口略去每场景最初至少 1s。任务运行时间按两个核的累计时间归一化；软件线程的总 CPU 毫秒可以大于单帧墙钟时间。事件窗口可能截断一帧，render 与 panel 次数可能有边界差 1；panel 指标是提交成功率，不是扫描完成率。各阶段等待/绘制会嵌套，不相加推导整帧时间。窗口加权吞吐与 LVGL 16 场景等权整数分数不同。

诊断会改变代码布局、调度和开销；只在同一诊断版本内比较，不覆盖之前的正式矩阵。每组一轮，不宣称小差值具统计显著性；串口通过不替代目视防撕裂验收。

首轮诊断的 90° 在关闭时报 ESP_ERR_INVALID_STATE；保留在 evidence/attempt1，不计为通过。当时还出现缓存计数饱和。修正版本把日志移到关闭之后，并每 250ms 左右采集/清零统计寄存器；成功记录要求 saturated=0。重跑通过不能证明首轮错误根因已完全查清，不能将其写成生产缺陷已修复。

原固件已完整恢复，16MiB 独立校验通过。二进制备份保存在 hardware-backups，不随报告交付。报告不含 build 或固件。

## 原始证据与官方口径

- [本轮逐场景诊断 CSV](scenes.csv)、[完整结构化记录](results.json)、[旧五板 2896 条逐场景原始统计](original-scenes.csv)。旧记录保持既有口径，并非本轮新跑。
- [五板配置审计](configuration-audit.json)、[本轮共享项及组件独有项](sdkconfig-pair-audit.json)、[生产源码一致性](production-source-proof.json)、[代码位置](code-placement.json)、[idle hook 审计](idle-hook-audit.json)、[本轮控制流程](events.jsonl)。
- [IDF S31 缓存计数语义](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-reference/system/cache_cnt.html)：accesses 为完成访问，stall_events 为缺失等待期间的重复事件，line_fills 才用于计算真实 miss ratio。当前固定 IDF 尚无该辅助 API，本轮直接读取其寄存器，未向 IDF 打补丁。
- [IDF S31 XIP 说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/api-guides/external-ram.html)。
