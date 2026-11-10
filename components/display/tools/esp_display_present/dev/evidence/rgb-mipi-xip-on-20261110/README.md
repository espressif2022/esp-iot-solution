# RGB / MIPI XIP ON 对比（2026-11-10）

[网页](comparison.html) · [CSV](comparison.csv) · [逐项数据/来源](matrix-data.json)

## 范围与执行方式

S3 RGB 480×480 / S31 Korvo RGB 800×480 / P4 MIPI DSI 1024×600。全部XIP/FETCH_INSTRUCTIONS/RODATA开启，按前述验证范围仅0°。FULL双/三缓冲、DIRECT、PARTIAL双/三缓冲、AUTO；NONE排除，adapter AUTO每板记−。没有运行XIP OFF；旧OFF记录不混当前比较。S3十行bounce（9600B×2），S31不使用bounce；两边各板panel都保留3张FB池，单输入tile，PARTIAL双方按逻辑宽×50行分配。S3当前present FB_IRAM开启，32KiB指令缓存/64KiB数据缓存；所有有效配置原件归档。

每板先完整跑干净IDF的present六项、adapter五项；随后单独跑隔离驱动轻量诊断的同样11项。每一有效项从open开始，连续完整16个benchmark场景，关闭成功才计PASS；本轮没有截取半场景、拼接被中断项目，也没有把先前测试分数混入。性能33项+诊断33项，共1056个完整场景，另有6个不支持条目。不得把这两轮加起来称66项正式性能分数。

## 数据定义

网页帧率使用LVGL完整16场景综合分，不是屏幕Hz。正式帧率与内存只取干净驱动轮，诊断轮不替换它们；adapter旧transport计数0不作为零FPS证据。

内存仅四个剩余值：before_open、after_open、RUN期间局部最低剩余、after_close；RAM/PSRAM分列，KiB。最低值取after_run的*_min_free，不是after_run的当前free。箭头均在present列，与adapter比较；剩余越多越好，RAM和PSRAM低水位不一定同时发生。输入缓冲容量运行时核对，驱动FB/旋转画布等架构实际分配并不人为填平。

## 轻量检测语义与边界

S3：独立读实际bounce源bb_fb_index，对照交还旧FB、取得可写FB、确认新FB及LVGL渲染开始时的地址。S31：读AXI预取描述符所属FB，修正GDMA全局组号1到AXI局部组号0的诊断转换；这代表DMA预取借用，不能冒充当前输出像素地址。P4：独立记录每次实际装载DMA链表所用的局部FB索引，生产者cur_fb_index的后续变化不会改这个快照；LLP只是上下文，未宣称等价实际逐像素扫描位置。三种检测口径在网页逐板说明。

地址命中按检查点累计，同一次提前交接可能对应确认、绘制两条记录，不等于独立撕裂次数。累计计数不截断，最多保存前16条地址样本。只有明确监测点才有覆盖；零命中不等于所有CPU/DMA写入都安全。取不稳或无法映射的源另记unknown，不当作零异常。独立输入tile等池外缓冲单独计数，不自动当成错误。S31描述符检查命中需结合预取语义复核。没有新增assert、故障注入、像素指纹、控制修正、额外延迟、回调替换或IRAM/时钟优化。诊断本身仍有短临界区开销，所以独立于正式性能轮。

RUN门控：RGB以VSYNC，DSI以现有帧DMA完成为边界，丢弃RUN入口跨阶段的首半帧。open/close不纳入；benchmark结束回调统计日志仍在RUN窗口内。S3补数EOF不足与既有自动DMA重启分列；S31/P4用原有硬件underrun中断观察，S3补数计数不适用于其他平台，不填假0。

[前一轮S3 adapter DIRECT曾命中的两组地址冲突](prior-s3-direct-findings.md)单独保留，未混入本轮计数。生产交接逻辑未修；本轮若未复现不能称已修复。

## 本轮检测记录

### s3-rgb

- present：本轮监测点未检出地址命中；未知源 0 次。
- adapter：DOUBLE_FULL: 4次地址命中；未知源 0 次。

### s31-rgb

- present：本轮监测点未检出地址命中；未知源 0 次。
- adapter：本轮监测点未检出地址命中；未知源 0 次。

### p4-dsi

- present：本轮监测点未检出地址命中；未知源 0 次。
- adapter：TRIPLE_FULL: 1次地址命中；未知源 0 次。

## 验证与归档

验证源代码冻结、基础IDF干净、真实esp_lcd组件路径、全部XIP标志、双方CPU/cache/PSRAM/LVGL有效公共配置一致、驱动原有控制语句未删除或重排、消费方hook剥离后等同生产控制流、16个唯一场景、非空绘制、单tile与容量、局部低水位、非零观察覆盖。Host ASan/UBSan包含正常/冲突/未知/阶段门控/DSI非bounce与underrun计数。浏览器逐格验证330个数值及120个内存箭头，桌面/手机无整页横向溢出、无下拉框。详见validation.json。

diagnostic-source.tar.gz包含两轮实际组件/测试源码与完整隔离esp_lcd，observer.patch对干净IDF；原始日志/SDK配置gzip、每套manifest/results/bench_cases和操作记录完整保留。无build、固件镜像、依赖缓存或flash备份。生产源码和基础IDF没有修改。按用户要求不恢复原固件，结束保留各板最后的XIP ON诊断固件。

当前页面只包含本轮三块RGB/DSI板，其他GRAM/TE以及先前角度数据属于历史，不拿来填补当前矩阵。
