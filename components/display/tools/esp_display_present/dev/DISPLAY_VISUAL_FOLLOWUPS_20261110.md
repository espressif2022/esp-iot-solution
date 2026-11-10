# 显示目视异常记录（2026-11-10）

## S3 RGB：Multi RGB、Container 场景严重撕裂和全屏抖动

- 状态：正在针对性分析；显示效果未通过验收。
- 用户最新确认：“RGB S3，大部分 multi RGB container 场景撕裂严重，甚至到了全屏幕抖动”。
- 初次报告时间：2026-11-10 13:11（Asia/Shanghai）。初次写作“S2 RGB”，后续用户已明确为 S3，旧称不再作为待确认板号。
- 实现范围：用户认为可能只有 present，但无法分辨，尚未形成与 adapter 的确定差异结论；须同配置对照。
- 首次报告时三块参测板均处于 present / XIP OFF / DOUBLE_FULL / 270°。该上下文仅用于回查日志，不将涉及多个场景的异常限定到单一模式或角度。
- 本轮 S3 双方均使用十行 RGB bounce（9600 B × 2）、同一屏幕时序。已有地址观察未记录到 S3 目标不一致，但不覆盖供数不足，也不证明不存在扫描或写入问题。
- 正在增加隔离诊断：统计驱动已有的每帧 bounce EOF 数不足及 DMA 自动重启次数；屏幕顶部标注当前实现和模式。只记录，不改变驱动原有判断、缓冲选择或恢复逻辑；不混入正式跑分。
- 串口 PASS 仅表示场景完整结束，不能视为目视无抖动、无撕裂。原因尚未确认，不能据此宣称修复。


### 已取得的客观证据

[14项完整诊断及结论](evidence/s3-rgb-supply-20261110/README.md)。16MHz/十行bounce窗口为325us，实测补数最长693.63us，其中memcpy为682.87us；一项16场景中895/4212帧补数不足并触发既有DMA重启。adapter同配置也存在同类不足，不能认定仅present有问题。

去掉重复同步、IDF 4KiB分段同步均未根治，CHUNK4K用户仍反馈明显抖动。40行bounce仍238/4181次不足，不能当作最终修复。仅将PCLK降到8MHz后0/2103次，但刷新率减半，仅用于归因。恢复16MHz、十行bounce、仅开启XIP后1/4200次，显著改善但未归零；这1次尚未确定发生阶段，不宣称完整修复。最后保留的是16MHz/XIP ON诊断固件，未烧回原固件。

供数超过扫描时限这一直接问题已证实；测量不能完全区分总线等待与更高优先级中断延迟，也不排除另有buffer生命周期问题。正式矩阵的性能成绩不得混入本轮诊断成绩。


### 单角度模式验证已完成

按用户要求取消继续补多角度，只按0°判定；复用已完整结束的模式，只补adapter缺失四项。结果：**present六种模式6/6、adapter支持的五种模式5/5，均在运行期间观察到补数EOF不足和既有DMA自动重启**。每项完整16场景，选定11项共176场景。adapter AUTO不支持记−，NONE未纳入。

[0°模式判定表](evidence/s3-rgb-xip-off-mode-check-20261110/comparison.html) · [完整来源、边界与验证](evidence/s3-rgb-xip-off-mode-check-20261110/README.md)。结论限定本S3 R16/GC9503、16MHz、十行bounce、PSRAM80MHz、XIP OFF与本轮benchmark负载；不能外推为所有S3硬件及任意配置必然异常。运行门控排除跨创建阶段的首个半帧，窗口外不足单列；原始日志、同版源码/同配置/驱动哈希及归档校验通过。

最后保留已验证的原16MHz、十行bounce、XIP ON诊断参考固件，不恢复原固件。生产代码和基础IDF保持不变。


## S3 XIP ON 单角度独立地址诊断已完成

[结果表](evidence/s3-rgb-xip-on-address-20261110/comparison.html) · [地址样本和边界](evidence/s3-rgb-xip-on-address-20261110/findings.md)。0°、present六模式和adapter五模式，每项完整16场景，共176场景，adapter AUTO不支持，NONE排除。16MHz、十行bounce、CPU240MHz、PSRAM80MHz、XIP/FETCH/RODATA开启，两边公共有效配置一致。

present六项在已覆盖的交还/取得/确认/绘制检查点未命中地址冲突。adapter DOUBLE_DIRECT检出两次确认错误，随后两次绘制目标仍等于实际bounce源：软件认为0x3c0e09c0提交完成，驱动仍在从0x3c151200取数，随后LVGL开始在0x3c151200绘制。其他四个adapter模式未命中。只能说明这些观察点的结果，非任意CPU/DMA写入的完全证明。

供数不足/既有重启分别计数：present各模式为2、3、2、6、5、8次；adapter为0、8、1、27、22次。RUN窗口包括benchmark结束统计日志，不能把所有次数归到视觉场景本身。不要混成buffer交换错误。

全量指纹版产生明显负载干扰，DOUBLE_FULL 2112/4201次不足、18FPS，已中止；轻量对照2/4193次、26FPS，最终独立地址版2/4195次、26FPS。弃用指纹版与取错scan字段的第一版地址观察均保留原始证据，不计入最终结论。

只在隔离IDF组件和present/adapter影子副本加计数/采样；正式src/include及基础IDF不改，不assert、不纠正或延迟交换。Host ASan/UBSan及非零覆盖验证通过。按用户要求不恢复原固件，结束保留adapter XIP ON地址诊断固件。本轮先记录，不改控制逻辑。


## RGB / MIPI XIP ON 全模式单角度对比已完成

[当前网页](evidence/five-board-regression-20261109/comparison-current.html) · [检测样本](evidence/rgb-mipi-xip-on-20261110/findings.md)。S3 RGB、S31 RGB、P4 MIPI DSI，present六模式、adapter五模式，0°；S3十行bounce、S31无bounce；NONE排除。全部XIP/FETCH/RODATA开启。33项正式帧率/内存与33项独立诊断，每项完整16场景，共1056场景；另6项adapter AUTO不支持。所有项目关闭成功，未中断或补拼半项；XIP OFF不再执行、展示或混入当前矩阵，旧记录保留为历史证据。

本轮S3 adapter DOUBLE_FULL两笔提交出现提前确认，随后的绘制目标仍被实际bounce取数；共4条检查点命中。P4 adapter TRIPLE_FULL首次提交前出现1条可写目标与已装载DMA帧地址重合，需核对共享panel重新打开时初始front假设，不能直接等同物理扫描撕裂。其余组合的已覆盖检查点未命中；全部unknown=0。前一轮S3 adapter DIRECT的两组既有证据独立保留，新一轮未命中不能说明已修复。

S31旧观察无法读地址的原因是GDMA全局AXI组号1误当局部寄存器组号0，已仅在隔离观察副本转换；本轮观察均有正数覆盖。P4使用实际装载DMA帧的独立快照，S3使用实际bounce源，S31使用预取描述符；口径分别说明。未对生产驱动、present/adapter交接或等待行为作任何修复，不新增assert。正式帧率和四时点剩余内存只取干净驱动轮，不混入诊断开销。

Host ASan/UBSan、22项运行工具测试、源/配置/固件链接路径/非零观察覆盖和浏览器330数值/120内存箭头全部验证完成。归档和客户交付SHA256已核对；归档没有build/固件/备份镜像。按用户要求不恢复原固件，结束保留各板XIP ON诊断固件。
