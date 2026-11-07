# S31 RGB DIRECT 优化与全模式补测

ESP32-S31 Korvo 原配800×480 RGB；两实现使用相同干净IDF、LVGL和benchmark输入。每项完整16场景，FPS为LVGL整数场景平均，单轮结果，不是光学刷新率。

| 模式 | 角度 | present | adapter |
|---|---:|---:|---:|
| NONE | 0° | 21 | 32 |
| NONE | 90° | 21 | - |
| NONE | 180° | 18 | - |
| NONE | 270° | 21 | - |
| DOUBLE_FULL | 0° | 22 | 23 |
| DOUBLE_FULL | 90° | 17 | 18 |
| DOUBLE_FULL | 180° | 15 | 16 |
| DOUBLE_FULL | 270° | 17 | 18 |
| TRIPLE_FULL | 0° | 25 | 27 |
| TRIPLE_FULL | 90° | 22 | 18 |
| TRIPLE_FULL | 180° | 20 | 16 |
| TRIPLE_FULL | 270° | 22 | 18 |
| DOUBLE_DIRECT | 0° | 23 | 24 |
| DOUBLE_DIRECT | 90° | 21 | 22 |
| DOUBLE_DIRECT | 180° | 17 | 19 |
| DOUBLE_DIRECT | 270° | 21 | 22 |
| DOUBLE_PARTIAL | 0° | 18 | 23 |
| DOUBLE_PARTIAL | 90° | 18 | 24 |
| DOUBLE_PARTIAL | 180° | 16 | 21 |
| DOUBLE_PARTIAL | 270° | 19 | 25 |
| TRIPLE_PARTIAL | 0° | 24 | 27 |
| TRIPLE_PARTIAL | 90° | 24 | 30 |
| TRIPLE_PARTIAL | 180° | 22 | 25 |
| TRIPLE_PARTIAL | 270° | 24 | 30 |
| AUTO | 0° | 24 | - |
| AUTO | 90° | 24 | - |
| AUTO | 180° | 23 | - |
| AUTO | 270° | 24 | - |

## DIRECT 前后对照

| 角度 | 修复前 present | 修复后 present | adapter |
|---|---:|---:|---:|
| 0° | 20 | 23 | 24 |
| 90° | 21 | 21 | 22 |
| 180° | 18 | 17 | 19 |
| 270° | 21 | 21 | 22 |

## 修改与范围

- 0° RGB565 DIRECT：LVGL在取得写租约后直接绘制双framebuffer；REFR_START发生在LVGL历史脏区同步之前，空刷新不申请缓冲。最后一次flush只提交一次完整同步图像，去掉中间画布复制、逐脏区租约开销和backend重复历史修补；下一次写入仍等待实际扫描退休。
- 旋转／输出字节序转换 DIRECT保留原copy路径，维持绘制与缓冲等待的重叠。实测借用逻辑画布导致提前等待和帧率下降，实验已撤回。
- RGB888、不兼容stride等布局保留原有copy路径。扫描退休协议、IDF、原始adapter没有修改。
- 保留独立fallback用于失败保护，不能把减少复制说成等量减少PSRAM分配。present完整矩阵每项结束检查LVGL绑定的sticky错误码，防止静默停止被算作PASS；这一检查在场景计时之外。修复前基线没有此收尾诊断，两次测试共享的benchmark场景源码、硬件初始化和关键配置一致。
- LVGL 9.5/9.6 ASan+UBSan各46/46项通过，覆盖脏区、字节序、转换延迟、空刷新、旋转切换、缓冲耗尽和扫描源不被提前改写。
- 旧版本为DIRECT四角度性能基线；原先28项固件在取齐DIRECT后主动停止，不能当作旧版完整28项PASS。修复版正式矩阵完整28项连续结束；adapter正式矩阵21项PASS、7项SKIP。
- 首次profile被外部下载握手中断，保留日志；重新校验固件后一轮四角度profile完整通过，插桩成绩不替代正式成绩。
- 本轮未进行新固件目视或光学撕裂验收；其他板卡没有重烧，网页保持它们优化前的历史成绩。
- mosaico原固件续写后整片16MiB校验通过；S31 RGB测试结束也恢复原固件并整片校验通过。Flash备份与build不放入交付件。
- 内存表沿用打开前剩余、打开后剩余、运行最小剩余、关闭后剩余；这些是整机对应时点的堆余量，不是组件静态数据区归因。首项present的LVGL首次初始化在open内，adapter在bench_init内，首项打开前的框架初始化边界不同；打开后、运行最小、关闭后更适合直接横向比较。

IDF commit: `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。完整日志、输入和固件指纹见组件dev/evidence/direct-borrowed-20261107。

## 原因与交接机制

旧DIRECT绑定使用独立LVGL画布：实际脏区再复制到空闲显示缓冲，backend补齐该缓冲缺少的历史区域。这两步在旧策略中各有职责；不是原代码已经让LVGL和backend重复同步。新0°路径把历史脏区同步交给LVGL，让它直接绘制已租借的两个物理缓冲。最后提交完整有效图像，因此backend无需再次修补。LVGL自身的同步复制仍然存在，不能把pixel executor没有job理解为整条显示链路没有复制。

物理缓冲必须先等可写再交给LVGL，因此减少数据搬运与保留渲染提前执行存在取舍。旋转路径借用转换画布的实验因提前等待而退化，已撤回；保留copy使绘制与转换/缓冲等待可以重叠。

## 分场景与计时解释

全部64条DIRECT场景前后及adapter对照见direct-scenes.csv。0°单轮空屏21→37、多矩形20→42；容器36→30、半透明容器29→22也有下降，均原样保留。0°独立单项测得22FPS，本轮完整矩阵为23FPS，不能把单轮整数均值当作所有场景稳定提升。

native DIRECT把部分退休等待放在REFR_START，早于LVGL的RENDER_START。因此flush均值从16ms降到0ms不代表省掉了全部16ms；以实际总FPS对照为准。CPU列也只是LVGL benchmark记录值，不是双核系统平均负载。

## 独立插桩验证（不替代正式成绩）

| 0°计数 | 修复前 | 修复后 |
|---|---:|---:|
| 输出提交帧数 | 1203 | 1419 |
| acquire/帧 | 3.49 | 1.00 |
| submit/帧 | 3.49 | 1.00 |
| backend硬件像素执行次数 | 2839 | 0 |
| begin均值（ms） | 5.508 | 9.944 |

这些计数来自两个独立带插桩轮次，每轮完整16场景。阶段时间可能嵌套或并行，不能相加。LVGL自带同步复制没有计入pixel executor；cache维护范围字节数也不是实际总线写入字节数。
