# 历史计划快照（2026-10-30）

> 以下原文保留用于追溯，不再代表当前执行计划。当前入口为 [PLAN.md](PLAN.md)。

# V2 八项代码收敛与验收计划

本轮按用户要求完成前八项代码工作，再统一 review。代码阶段完成本地 Host 与离线编译；随后已启动 S3/P4 独立测试工程上板验收，首轮 DOUBLE_FULL 通过，详见下文。V1 与现有产品工程不变。本文为当前状态，早期分阶段记录不代表现状。

| 项 | 交付 | 当前结果与限制 |
| --- | --- | --- |
| 1 | TE／FB 非阻塞 repair、prefill | 异步 ticket 驱动，service 每次只推进已完成阶段；取消／stop 超时保留源和目标；同步默认路径保留 |
| 2 | 运行时旋转与触摸逆变换 | GRAM／TE／FB 四方向、非方形画面；Presenter drain 后切换，超时不改配置；FB 保留物理扫描池，另建逻辑绘制池 |
| 3 | Presenter 显式 receipt／tracking | commit/query/wait/release/cancel_tracking；32 个观察记录；取消观察不取消传输；不冒充 V1 generation／VSYNC 协议 |
| 4 | P4 DSI scanout 与 PSRAM/cache 边界 | 真实 IDF adapter、驱动 FB 池、FB0 初始扫描保留、两次提交后帧边界确认、C2M 与安全停止；真实 PSRAM 一致性待上板 |
| 5 | RGB scanout | 复用同一 adapter 的 RGB bounce 连续刷新路径；明确拒绝非 bounce 模式，后者缺少当前平台统一可验证的退役语义 |
| 6 | DMA2D／PPA executor | 独立执行器、串行共享、完成后归还借用、cache line／MSPI 约束、提交前 CPU 回退；DMA2D 用本地 IDF 6.2 私有 API，升级需复核 |
| 7 | LVGL 零拷贝 | 可选 FULL／RGB565/native／兼容 stride／多 FB；RENDER_START 先 acquire，flush_ready 前解绑；保留失败准入的私有安全缓冲。PARTIAL、DIRECT、RGB888 保持复制路径 |
| 8 | 统一 review、回归与文档 | 普通 Host、ASan/UBSan、P4 编译；说明 API、资源代价、支持边界与后续硬件验收，不将模拟结果当成无撕裂证明 |

上述第 4～7 项提供了明确限定的实现，不表示支持所有 IDF 版本、RGB 扫描模式或所有 LVGL 零拷贝组合。未承诺旧 API 二进制兼容。

## 本地验收

- 17 组核心：records、coverage、repair、accel、scanout、rotation、pixel、gram、lcd_spi、presenter、te_composer、te、presenter_te、fb、event、scheduler、headers_cpp。
- 4 组真实 LVGL 9.5：lvgl_zero、lvgl、lvgl_te、lvgl_fb；合计 21 组。
- repair 用条件变量阻塞执行器，检查 service 不阻塞、候选帧不提前发送、prefill 未完成不借出、取消／stop 保留和重试。
- rotation 覆盖初始与运行时四方向、三个后端、RGB565／RGB888、同步／异步、外部扫描缓冲与逆坐标；独立像素期望值对照。
- accel 编译生产适配器，驱动 stub 独立解释描述符；验证像素、旋转方向、字节序、cache 对齐、MSPI 限制、早到完成、错误后不重复 CPU 拷贝及销毁失败重试。
- scanout 验证初始扫描 FB 不可写、提交期间边沿不冒充确认、两边界确认、不确定接受隔离和 stop 重试。
- LVGL 零拷贝验证实际 lease 地址、完整像素、pending／scanning 源不被重写、无脏区不申请 lease、超时暂停、销毁 binding 后传输仍可完成。
- repair／rotation／scanout／accel／lvgl_zero／presenter 六组各重复 100 次通过。P4 + LVGL 与 ESP32-S3 核心 smoke 均完成离线编译链接。
- ASan/UBSan 使用 `detect_leaks=0`；当前环境未验收泄漏检测、TSAN、竞态穷举或真机时序。

```sh
python3 tools/dev.py test --with-lvgl
ASAN_OPTIONS=detect_leaks=0 python3 tools/dev.py test --with-lvgl --sanitize
# 激活本地 ESP-IDF 后，只编译，不烧录
python3 tools/dev.py idf-build --target esp32p4 --with-lvgl
```

## Review 顺序

1. `include/present_backend.h`、`present_presenter.h`：lease、receipt、旋转和 stop 契约。
2. `src/backend/te_composer.c`、`fb.c`：repair/prefill 与长期扫描借用。
3. `src/platform/lcd_scanout.c`：事件到退役证据的映射，驱动版本假设。
4. `src/platform/pixel_accel.c`：硬件准入、cache、失败和完成屏障。
5. `bindings/esp_lv_present_v2/present_lvgl.c`：绘制前取得写权限，错误路径解绑。
6. `review.md` 的已处理建议与保留项，结合上述生产源码测试阅读。

## 统一上板阶段（已启动）

已核对并完整备份原 Flash：S3 `/dev/ttyUSB0`，MAC `90:e5:b1:d0:a9:d8`，R16、480×480 GC9503 RGB565；P4 `/dev/ttyACM0`，MAC `e8:f6:0a:e7:a7:8d`，芯片 v3.2、1024×600 EK79007 MIPI RGB565。PSRAM 分别 16/32 MB。

2026-10-30：独立工程 [test_apps/boards](test_apps/boards/README.md) 完成两板编译、烧录和 CPU DOUBLE_FULL 首轮验收。两板日志均覆盖 frame 0..240，PRESENT_CONFIRMED 成功、pending=0、sticky=ESP_OK，采样内部 heap 保持不变；用户确认画面正常。保留两次边界确认，没有降低阈值。详见 [RESULTS.md](test_apps/boards/RESULTS.md)。

第二轮五种模式首轮硬件验收已通过：S3 3 轮、P4 2 轮，整帧像素一致，stop/recreate 与 heap 检查通过，用户确认画面正常。三缓冲目前仅验证配置和生命周期。后续：三缓冲实际第三缓冲使用/排队背压 → LVGL → 旋转/触摸 → DMA2D/PPA 对照 → 长时间压力。尚未验收的模式不因 FULL 通过而判定通过；两板结果不能替代 SPI/TE 实屏时序验收。

## 独立后续项

非 bounce RGB 的平台专属退役协议；更多 LVGL 零拷贝组合；V1 generation/VSYNC 兼容层；裸后端及 SPI 等待的全面事件化；减少 DIRECT 防御性 prefill+repair 写放大；硬件性能调优。它们不混入本轮已通过的能力声明。


## Review 修改验收（2026-10-30）

已落实：目标行带 cache 同步、coverage 字节批处理、旋转回零释放逻辑池；同步修改资源契约与命名风格。新增 coverage 组，普通 Host 与 ASan/UBSan 均为 20/20（detect_leaks=0）；P4 + LVGL、S3 核心离线编译通过。Doxygen XML 生成零警告；coverage、rotation、accel、repair 四组各重复 100 次通过。旋转测试覆盖像素池内存水位恢复；cache 测试核对准确的同步区间和边界。

DSI 两次边界规则保持不变。本地驱动源码显示 DMA 完成后重启持续扫描；正式上板先做单次提交后持续边界计数，不通过降低阈值绕过异常。用户确认：P4 ACM0 为 1024×600 EK79007 配置，S3 USB0 为 480×480 GC9503CV 配置；该代码 review 阶段未上板；后续首轮上板结果见上文。

## 公共记录规则收敛（2026-10-30）

新增私有 `src/present_records.h`，三个后端共享以下规则：

- `pv2_record_t`：token、external、terminal、首次错误；TE/FB 直接使用，GRAM 以首成员组合并保留 pending/leased/sealed/canceled。
- 按实际记录 stride 查找 token；是否要求 external 由调用点指定，不跨实例共享池。
- 统一公共状态输出、首次 record error 与 sticky fault 保留、最近一次完成取消的幂等记录。
- 统一防溢出的矩形/脏区校验及 stop 剩余超时预算。

模块不持有锁，不调用驱动，不释放像素，不选择完成时机，不统一后端状态机。
调用者继续持有所属后端锁；ISR 会调用的错误/取消发布函数保留 ISR 属性。
GRAM 的 terminal 仍由 sealed/pending 推导；TE 的传输终态不冒充显示确认；FB
只在成功确认时声明 present_confirmed。池大小、空槽分配、回收与缓冲所有权仍
由后端管理，避免引入第二个资源状态机。当前没有新增时间戳字段；未来新增时
需先定义事件语义，再扩展公共记录/输出入口以及各后端真实完成点。

新增 `records` 测试包含组合记录 stride、零/旧 token、external 限制、首次错误、
失败终态与显示确认区别、矩形溢出、超时边界，以及同一套 GRAM/TE/FB 契约检查：
非法 begin 不产生 token、重复取消成功、旧取消 token 失效、超过池容量次数的
begin/cancel 循环正常复用、取消后 receipt 不可查询且不污染 fault。

验收：普通 Host + LVGL 21/21；ASan/UBSan 21/21（detect_leaks=0，未做泄漏检测）；
S3 核心与 P4 + LVGL smoke 离线编译通过。本轮未连接或烧录设备，之前板测结果
对应重构前固件，不能当成本次修改的板上回归结果。

## QSPI TE 与流程收敛（2026-10-30）

FB 推进结果已明确分离 IDLE/PENDING/COMPLETE/FAILED 与 esp_err_t，封帧尝试/已接受操作采用命名上下文；文档同步。普通及 sanitizer Host 21/21，scanout/repair/fb各100次通过，S3/P4/S31离线构建通过。S31新板全屏GRAM 8帧与TE 64×16 ROI 1200帧通过、零窗口超出；不能外推全屏TE或光学无撕裂。详情见 [板测记录](test_apps/boards/RESULTS.md)。本轮未重刷S3/P4。


# S0/S1 后、整体推进前的计划快照

# V2 全局设计重审与实施计划

更新：2026-10-30。用户已批准开始整体实施；当前已进入 **S0/S1 生产代码迁移**。Executor 前置切片已完成；S0 规划器与 S1 GRAM 公共帧/事务核心已落地，整体 S0～S6 尚未完成。下方 P0～P7 是早期设计阶段记录，以本节实施状态为准。

## 当前实施主线

| 切片 | 当前状态 | 未完成门槛 |
| --- | --- | --- |
| Executor 前置 | 完成 CPU/PPA/DMA2D 插件和 P4 像素路径验证 | 不等于全架构完成 |
| S0 | 已实现纯配置规划、独立池、旋转拓扑预算、能力查询及区域协商 | metadata/driver/stacks 的整体预算；新运行时路径接入 |
| S1 | GRAM 使用公共帧核心及事务接受性/完成归并；旧重复帧字段已移除 | Allocation 的 B/C/T/S 引用账本、统一 lease 操作；TE/FB 尚未迁移 |
| S2 | 待实现 | 共享完整图像构建器、精确 coverage、有界版本 history/repair |
| S3 | 待实现 | TE 单/双 compose、真实跨帧流水、动态相位准入 |
| S4 | 待实现 | FB 接入共享构建器/核心、旋转逻辑池解耦 |
| S5 | 待实现 | Presenter/LVGL 协商、reconfigure/tracking 完整贯通 |
| S6 | 局部硬件证据保留 | 新拓扑/TE 完整板测、性能、压力与峰值内存 |

执行顺序仍是 S0 → S1 → S2 → S3 → S4 → S5 → S6；不会以插件拆分替代整个计划。新入口 `pv2_presenter_create_planned()` 对 `pending_runtime != 0` 在分配/attach 前拒绝；旧入口保持既有行为，不能把旧实现的运行结果算作新拓扑验收。

本轮最终 Host + LVGL **27/27**、ASan/UBSan **27/27**；sanitize 使用 `detect_leaks=0`，未验证泄漏。S3、P4+LVGL 离线编译通过。新增 plan/frame_core 测试；保留早到完成、取消/stop 超时重试及异步发送回归。Host 不等于竞态穷举或真实无撕裂证明。

设计入口：[ARCHITECTURE.md](ARCHITECTURE.md)。这是待 review 的目标草案，不是当前实现说明。

## 基本约束

- 先盘点全部模式、能力和失败语义，再设计；不在三个后端中逐项塞功能。
- 模式是受约束的组合预设，不为每个模式复制生命周期。
- 区分公共语义和硬件事实；既不保持三套重复系统，也不建万能 stage 表。
- 已有代码/Host/板测保留为证据，不等于迁移完整或新架构已通过验收。
- 先交付 P1～P3 供用户 review，再进入生产代码改造。用户已要求必要测试上板：实现切片达到 Host 门槛后执行对应硬件验收，当前设计阶段不为重复旧结果而烧录。

## 阶段与交付

| 阶段 | 工作 | 交付及通过条件 | 状态 |
| --- | --- | --- | --- |
| P0 | 保存架构草案、重置计划、归档旧进度 | 模式矩阵、架构图、流水线和所有权原则可本地 review | 完成 |
| P1 | V1/V2 全量能力盘点 | 每项列源码依据、默认/限制、V2 差异、目标归属、支持/拒绝决定、测试证据；覆盖所有模式及横向能力 | 第一轮盘点完成，见 MIGRATION_AUDIT.md；目标决策待 review |
| P2 | 所有权与运行时序设计 | 逐步标注 buffer/版本/读写者/推进事件/超时资源；定义事件、锁、队列满、取消、故障、stop；计算实际分辨率内存 | 时序草案已交付；见 DESIGN_REVIEW.md 的契约与阻塞项 |
| P3 | 架构和接口 review 包 | 确定公共核心与输出策略边界、配置模型、接口契约、旧代码取舍；列出尚未决定事项，用户 review 后再实施 | review 包已交付，待 review；未进入生产代码修改 |
| P4 | 共同生命周期与资源规则 | 基于已审设计小步迁移；旧契约回归和跨输出公共契约测试通过；不同时改全部路径 | 未启动，依赖 P3 review |
| P5 | 图像构建与输出策略接入 | 共享内容版本/coverage/repair；按 GRAM → TE → FB 切片，每片覆盖全部对应配置与异常，不留下第二份权威状态 | 未启动，细分顺序待 P3 确认 |
| P6 | Presenter/LVGL/平台集成验收 | 模式配置、真实 LVGL、异步/加速、旋转、内存能力与零拷贝；Host 普通和 ASan/UBSan、离线编译 | 未启动 |
| P7 | 独立硬件验收 | 经 review 再执行 S3 RGB/P4 MIPI/S31 QSPI TE；真实多缓冲重叠、全屏 TE、长期和性能验证，记录每项限制 | 暂停 |

阶段完成以证据为准；不把写了文档或新增函数视为对应能力完成。

## P1 盘点模板与范围

每项使用：`ID / 模式或能力 / V1 行为和源码 / V2 行为和源码 / 差距 / 目标归属 / 支持或拒绝理由 / Host 验收 / 硬件验收 / 状态`。

必须包含 NONE（IO/FB）、DOUBLE_FULL、TRIPLE_FULL、DOUBLE_DIRECT、DOUBLE_PARTIAL、TRIPLE_PARTIAL、TE_SYNC 单/多 compose、AUTO，以及 V2 的显式配置扩展。

横向项必须包含：多 tile/完整图像/暂存分别计数，TE 动态相位，真实跨帧并行，内容版本/repair，首帧覆盖，RGB565/RGB888，旋转/字节序/stride，原地转换，异步 copy 和加速器，PSRAM/cache/外部 FB，LVGL 三契约/零拷贝，damage/scroll，generation/tracking，事件/线程，取消/停止/故障和初始扫描所有权。

## P2 必须推演的时序

1. GRAM 三块分别绘制、转换、发送；封帧前已有硬件访问。
2. TE 单 compose 的复用与取消；双/多 compose 的当前帧发送、下一帧构建、repair 基线引用。
3. TE 窗口错过、拒收后重试、边沿过期、无安全窗口及测量不足。
4. 双 FB FULL/PARTIAL/DIRECT 的写权限和旧内容准备。
5. 三 FB 的扫描、待切换、构建及增量依赖；前序失败后的后继处理。
6. copy 延迟、早到回调、明确拒收、不确定接受、无回调和故障。
7. cancel/stop(0) 超时后重试；事件源停止、回调排空与资源释放。
8. rotation 排空、外部 FB、初始扫描源、无 receipt 回收、多实例隔离。

内存预算使用真实屏幕 480×480、1024×600，分别计算 tile、完整图像、旋转逻辑池、coverage、对齐和必要 DMA 暂存。不能通过缩小测试画面回避方案不适配目标硬件。

## 实施与验证纪律

P3 后的每个实施切片都要列明：迁移哪些契约、删除哪些旧职责、保留哪些接口、如何独立回退、哪些测试证明等价。不能同时留下新旧两份 buffer/frame 权威状态。

Host 使用独立参考像素和可控事件交错，不用任意 sleep 代替同步。普通 Host 与 ASan/UBSan 均需通过；若 detect_leaks=0，明确未做泄漏检测。不声称竞态穷举或模拟窗口等同真实无撕裂。硬件验收以新代码对应固件为准。

## 历史证据

旧计划原文完整归档于 [PLAN_HISTORY_2026-10-30.md](PLAN_HISTORY_2026-10-30.md)，其中“当前”“完成”均属于归档时的局部实现描述，不是本计划状态。

已有记录包括 Host 21/21（ASan/UBSan 关闭泄漏检测）、S3/P4 五种 CPU 模式基线、S31 全屏 GRAM 和 64×16 TE ROI。见 [板测结果](test_apps/boards/RESULTS.md)。原架构草案落盘时仅修改文档；本轮P1已重新构建并运行Host基线，见下文。历史板测未重新执行。

纠正：此前 539 μs TE 高脉冲只用于保守 ROI 测试，不代表全部合法发送相位；该结果未覆盖 V1 完整 TE 能力。

## 本轮进度

- P1 交付：[MIGRATION_AUDIT.md](MIGRATION_AUDIT.md)，11个模式条目、24个横向能力、7个设计决策及三板分工。
- 新发现：V1公开compose最大2、V2 FB PARTIAL仅单scratch、AUTO默认不同、scroll/对齐协商缺失，以及V1 TE超时仍发送的退化行为。
- P2 交付入口：[OWNERSHIP_SEQUENCES.md](OWNERSHIP_SEQUENCES.md)，设计推演，不是当前实现状态。

### P1 当前源码基线复验

普通 Host + LVGL 21/21，ASan/UBSan 21/21，均重新构建后运行；ASAN_OPTIONS=detect_leaks=0，未做泄漏检测。本轮无生产代码修改、无烧录。日志保存在忽略目录 `test_apps/boards/artifacts/p1-baseline-host.log`、`p1-baseline-sanitize.log`。这些结果不覆盖新架构或审计发现的缺失能力。文档链接、代码块闭合及审计ID数量已检查。

## P2/P3 交付

[DESIGN_REVIEW.md](DESIGN_REVIEW.md) 汇总D01～D10、最小职责接口、seal与READY分离、完成事实、配置兼容、TE准入输出及S0～S6实施切片。当前仍是待review设计，P4未启动。平台TE扫描模型和外部内存传输能力明确列为上线前阻塞项，不假定由旧算法自动证明。

## 数据面review补充

已纳入用户提出的三个跨层问题，形成DESIGN_REVIEW的D11～D13和第13～16节。S0必须交付旋转拓扑、分离池数量、能力反馈和完整预算；S2必须同时交付版本damage history与精确coverage，不能先用全屏补集repair再等性能阶段返工。旋转、差异修补、对齐新增Host与板测门槛已列出。本次仍是设计文档变更，未修改生产代码，未声称新测试通过。

## Executor前置切片：已实现并验收

- 独立CPU/PPA/DMA2D插件、能力查询、无副作用probe、统一选择/执行、旧API包装已落地；没有修改显示后端生命周期或资源拓扑。
- Host + LVGL 25/25；ASan/UBSan 25/25（detect_leaks=0）；四种独立插件链接组合通过；S3和P4+LVGL离线编译通过。
- P4重新读取MAC并烧入对照固件，32例全部通过：PPA硬件16例，DMA2D硬件4例，DMA2D旋转CPU回退12例；包含内部/PSRAM、RGB565/RGB888。
- 此项是S0能力接口前置，不标记S0或P4～P7整体完成。全局方案仍有各切片自己的验收要求。
