> 当前执行（2026-11-03）：生命周期、符号与私有导出收敛已落地，核心／binding 已并入 `esp_display_present`／`esp_lv_present`；Host、三目标离线与 GSP 链接通过，最终记录见新组件 MIGRATION_STATUS.md。旧目录仅用于历史记录，不再是生产组件。详见 [执行记录](TRANSITION_DESIGN.md#13-执行记录两组件候选2026-11-03)。未上板、未发布、未提交。

## 下一轮：旧契约与 V2 状态收敛（2026-11-03）

设计与逐项验收以 [TRANSITION_DESIGN.md](TRANSITION_DESIGN.md) 为准。下列步骤已完成至本地 T3 验收；T4 上板尚未执行。

1. T0：核对字段读写与契约，补基线缺口。
2. T1：先收敛撤销／关闭；核心管理执行事实，门面保留旧句柄及应用意图。
3. T2：再拆 poll 的时间线投影、receipt 回收、tracking 水位；额外两个原始 VSYNC 语义不变。
4. N0／N1：先盘点公开／私有符号，再独立迁移为正式 `esp_*` 命名；`pv2_image_repaired` 作为私有操作改名为 `present_image_mark_repaired`。兼容、导出和目录边界见设计第 11 节。
5. N2：N1 完成后将核心和 binding 分别合并到 `esp_display_present`、`esp_lv_present`，同步构建与交付依赖。
6. T3：全量 Host／sanitizer、默认入口离线构建与 GSP／LVGL 链接评审。
7. T4：三板候选短回归后收口；不以历史板测代替。

各阶段独立评审：旧应用 API 保持不变；扩展与内部接口命名单独迁移，不增加模式、不夹带性能优化。N0 可提前盘点，N1 在 T2 后执行，N2 随后实施，T3 验收最终候选。生命周期独立复审、命名迁移、组件合并与最终本地验收已完成；实板和发布另行验收。

> 2026-11-03 默认入口已切到 V2，旧实现已移除；当前布局、支持范围及验证见 [DEFAULT_SWITCH.md](DEFAULT_SWITCH.md)。下文旧阶段中的“尚未切换”与 V1 源码路径只用于历史追溯。

> 2026-11-03：完成兼容 TE 非阻塞准入、未封帧 stop／TE 取消收尾、自动单 tile 回退和独立组件交付修复。最新验证与限制见 [review.md](review.md)。

# V2 短期收敛计划

本轮代码、性能和未通过项统一见 [收敛验收记录](test_apps/legacy_compat/RESULTS_CLOSE_20261102.md)。

目标：普通应用用一套应用级入口获得经过验收的默认路径；原调用方按已审计契约迁移；保持现有核心、缓冲所有权与驱动边界证据。不是再增加模式或追求所有场景压过 V1。

本计划是后续执行顺序；此前 S0～S6、跑分与驱动专项均保留为历史证据，不等于下面的兼容/内存工作完成。

| 阶段 | 工作 | 完成门槛 | 当前状态 |
| --- | --- | --- | --- |
| C0 契约与调用面 | 修 source_released；审计 V1 公共头文件、三示例、LVGL 和实际预编译 GSP | gated async copy 的 query/wait 不提前完成；失败/stop 不提前释放；兼容清单有代码/二进制依据 | 已完成：本地验收通过 |
| C1 平台工厂 | 门面内部集中管理 adapter、外部 FB、执行器、service、TE 事件源的创建与销毁集中到应用级入口 | 创建失败逆序清理、销毁失败保留可重试；普通调用方不用组装虚表；输出 resolved 配置和实际执行器 | 平台装配、TE gate、单 FB、RGB/SPI 只读查询已落地，支持域 Host/实板通过 |
| C2 V1 门面 | 按原头文件逐符号实现；同名实现构建互斥；LVGL 生命周期接现有安全共享池 | 三示例只换组件选择而不改业务源码；原 GSP 预编译库实际链接；tracking/generation/scroll/故障逐项验收 | Host 34/34；固定尺寸旋转/安全 staging/128 脏区已补；GSP 1.5.1 显示及 S31 TE 通过，明确拒绝域保留 |
| C3 默认配置验收 | 门面默认配置运行三板官方 benchmark、handoff 和异步异常测试 | 与同配置 V1 对比；无手工性能胶水；保留逐场景数据；未达线先查实际执行路径 | P4 交接及 GSP 显示已确认；三板公平对照完成；P4 V1/V2=28/27，S3=19/17，S31=25/25 FPS |
| C4 覆盖与内存 | 去掉常规矩形路径的常驻全屏位图，精确矩形/区间差集与有界退路 | 1024×600 常规路径省下 75 KiB；原逐像素参考不放宽；溢出不能覆盖本帧新像素；规划预算同步更新 | 已实现并通过 Host：常见矩形无常驻位图，碎片化时惰性分配，失败不发布图像 |
| C5 条件性调度优化与切换 | 只有 C3 实测有必要时再动锁/唤醒；通过后切换示例默认依赖 | 分解 acquire/copy/repair/cache/fence；有收益才保留；V1 留作对照，不强制追逐最后 1 FPS | 区间优化有效，保留；默认依赖暂不切换，明确拒绝域和剩余性能差距留待单独评审 |

当前表格为最新状态，后文逐轮记录保留各自时点。尚未切默认依赖。

## 固定的设计边界

- 普通入口负责 IDF 平台装配，V2 核心仍可显式注入并用于 Host。不把 SoC 分支塞回公共像素执行流程。
- 源码兼容、行为兼容、二进制兼容分别验收，不承诺几百行实现全部兼容。GSP 库依赖符号与头文件 SHA256 见 V1_API_AUDIT.json。
- V1 的 FAULTED 不在原实例恢复；V2 已接受操作的 sticky fault 也不自动清除。仅准入/明确拒收和 stop 排空超时可按现有契约重试。
- `quiesce` 只排空，不等同于销毁 panel；不得把仍需恢复使用的共享显示资源交给删除式 stop。
- receipt/generation 与 tracking 用门面自己的记录管理；不在 get_status 时悄悄释放还允许重复查询的观察结果。
- 32 位 V1 lease_id 与 64 位 V2 token 需要映射，不允许直接截断。
- 保留 LVGL 准入失败时的安全目标缓冲。原生双 tile / 删除 fallback 必须另有“开始绘制前已获得 lease”的证明，不能作为门面前置优化。
- 默认继续使用已验证的双边界 scanout fence；RGB 无 bounce、单次刷新不伪装成已支持模式。
- TE 定时不能靠猜测生成。需要明确时序来源/测量过程，未支持配置创建前拒绝。
- coverage 溢出后的全屏重绘是生产者重新渲染，不是全屏复制基线。信息丢失后禁止用基线覆盖新像素；阶段 C4 前必须选定明确的重绘/拒收协议。
- 不加入新的后台层、全局注册中心、模式分支或多 producer。已有 sender 足够时复用。

## C0 状态报告规则

第一切片采用保守语义：SOURCE_RELEASED 不承诺最早源释放时刻；在帧的已接受访问和内部流水线排空、帧进入终态后才报告。它不是再次写入当前扫描 FB 的权限，重新写入仍必须 acquire lease。终态失败只有排空后才能报告源释放，不代表成功显示。

Host / LVGL 和 ASan/UBSan/LSan 为每个代码切片的门槛；硬件用例只对实际烧录版本生效。C0 修改完成后不会把旧驱动专项当成新固件板测。

## C3 性能目标的口径

- P4 0° 官方 benchmark 以当前 V1 42 FPS / 已调 V2 41 FPS 为参考，不保证 render/flush 归因数值相同。
- S3/S31 与同轮同配置 V1 对比，目标消除默认接入退化；达不到则给出具体场景和原因。
- P4 FULL 90° 的旧 18.2→30.1 FPS 是特定合成负载证据，不当成官方 LVGL 整体 +65%。
- 不凭现象认定“所有差距来自锁”；不把 copy worker 改成每帧才唤醒，否则少量 tile 时可能无法继续绘制。

## C0 已交付（2026-11-02）

- `present_frame_core.c` 不再从 supports_present 推导源已释放；必须帧完成且无 pending/lease。公共状态注释明确“不授予扫描 FB 写权限”。
- 修正原 frame_core 测试中错误的提前释放断言。
- `test_repair.c` 新增 gated PARTITION copy 的成功、执行失败、stop(0) 超时三个后端场景，以及通过 Presenter receipt/query/wait 的应用侧场景；全部使用条件变量，不靠任意 sleep。
- 复制阻塞时 query=false / wait(0)=TIMEOUT / receipt 不能释放；失败和 stop 只有排空后报告源释放，sticky fault 不清除。
- 调用面：21 个 V1 公共符号；三平台 GSP 1.9.0 预编译库均实际引用 14 个 presenter 符号。逐项差异见 [V1_API_AUDIT.md](V1_API_AUDIT.md)。
- 验收：Host/LVGL 31/31；ASan/UBSan/LSan 31/31（detect_leaks=1）；frame_core/repair/fb/presenter 各连续 20 次通过；S3/P4 隔离应用离线编译通过。本轮没有烧录，前一轮驱动轨迹仍只对应其归档固件。

C0 时点 C1/C2 尚未实现，不声称旧示例或 GSP 已完成兼容。C1 作为门面内部装配实现，不再额外推出一套要求普通应用调用的平台 API。

## C1 基础装配切片（2026-11-02）

资源、执行器、驱动 FB 获取、初始扫描源建立、后台 service 和可重试销毁集中在 `../esp_display_present/src/resources.c`；不是公开应用 API。借用 panel 的最终 detach 与正常 drain 分离。详见 [资源层说明](../esp_display_present/README.md)。

Host/LVGL 已扩展为 32 项（新增 legacy_resources）；scanout 增加原始 VSYNC、pending stop、取消注册失败和迟到 ISR 模型。S3/P4 只做离线构建，未烧录新路径。原默认 stop 和 fence 不变。

C1 时点未完成项包括 TE、单 FB 及其他被明确拒绝的配置；当时 C2 同名入口尚未启用，当前进度见下文。不能把私有资源层或复制了头文件说成已经零修改迁移。

C1 本切片验收：Host/LVGL 和 ASan/UBSan/LSan 32/32；scanout / legacy_resources 各连续 30 次通过；S3/P4 强制链接资源层的离线 smoke 通过。V1 三份头文件与审计 SHA256、原件逐字节一致。未烧录；C2 同名函数与旧 LVGL 入口尚未实现。

## C2 基础门面切片（2026-11-02）

- 新增可选择的 `../esp_display_present` / `../esp_lv_present` 组件，同名实现互斥；原组件和示例默认依赖未切换。核心不导出 V1 符号，也不依赖旧头文件。平台资源聚合移到门面组件内，Host 单独链接测试。
- 21 个 V1 符号均有实现，三个 presenter 公共头文件及 LVGL 头文件逐字节保留。32 位 lease 序号映射到当前 64 位 lease，过期 ID 不可提交/resolve，序号耗尽不回绕。
- 私有控制桥通过核心 gate 观察状态/receipt，允许 producer 已退出后的控制任务 stop/destroy；公开 V2 任务归属限制不变，begin/acquire/submit 仍单 producer。状态字为原子类型，stop 的 closing 发布不与查询竞争。
- tracking 观察与硬件所有权独立；取消观察不取消传输。确认在观察到成功 fence 后再等两个原始 VSYNC，允许保守延迟，不将 VSYNC 当作 buffer 退休。旧 tracking 的确认阈值不能污染后续帧。
- 首帧、scroll、取消后重建及 LVGL 交接后要求全绘，在 raster 前返回实际区域。scroll 不伪装成已完成的滚动拷贝。
- LVGL 旧入口连接现有 V2 pooled PARTIAL / zero-copy FULL，其他已支持布局使用安全复制绑定；没有重新实现 flush 或引入线程。LVGL 提交与其他 producer 共用核心单调帧序号。
- 新增 `legacy_facade`：Host 六种 FB 配置 8×4 像素对照、GRAM 延迟/早到完成、跨控制任务查询/销毁、lease 过期、rotation/generation、scroll、tracking 取消与实际 LVGL 交接。新增原示例隔离准备工具 `test_apps/legacy_compat/prepare.py`。

本切片不是 C2 全部完成：TE/单 FB 等 C1 拒绝项、逐笔 push 的精确 timeout（当前为查询时的封帧队列停滞诊断）、原三示例和 GSP 完整链接/运行仍须逐项收敛。当前不改变默认组件，不用符号齐全替代行为兼容。C3 板测、C4 75 KiB 位图收敛、C5 条件性调度优化仍未完成。

本轮 C2 基础切片最终结果：Host/LVGL 33/33、ASan/UBSan/LSan 33/33（泄漏检测开启）、legacy_facade 连续 50 次；S3/P4 组件离线 smoke 及三个原示例的 P4 离线链接通过。showcase 使用缓存 GSP 1.9.0，不等同于原 1.5 发布组合。未烧录、未跑分；详情见 [C2 RESULTS](test_apps/legacy_compat/RESULTS.md)。C2 整体仍未验收完成。

## C2 第二轮：行为差分与传输期限

修正 full=true/count=0、非法 dirty/scroll 校验、PARTITION scroll 拒绝、producer band 高度对齐/16 位行数、GRAM 所有权水位、已到达 tracking 确认及 fault 下 handoff/quiesce。直接编译 V1 几何实现做 700 组差分和非方形四方向像素对照。旧 API 非方形运行时跨轴明确拒绝，原生 V2 旋转语义不改。

移除封帧队列停滞近似；现有 Presenter 在物理 submit/完成入口统一逐笔计时，期限故障不释放源、不伪造完成，不复制三套后端逻辑。启用时才分配有界元数据并纳入预算；原 sender 按最近期限唤醒，没有新任务。同步无 sender 时仍为协作检查，不能中断阻塞的驱动调用。

最终 Host/LVGL 34/34、ASan/UBSan/LSan 34/34，三项相关回归各 50 次；S3/P4 组件及三个原示例 P4 离线链接通过，格式/补丁检查通过。未烧录；完整证据与剩余门槛见 [C2 第二轮 RESULTS](test_apps/legacy_compat/RESULTS_C2_CONTRACTS.md)。TE 时序+DMA 内存事实+事件源生命周期、单 FB、非方形跨轴旧契约、原 GSP 发布组合与 C3～C5 仍未完成，不能标记 C2 整体关闭。

## C3 首个实板切片：P4 交接

原 GSP/LVGL 交接通过一整批 5 轮，另有第 6 轮通过，零错误，每轮 LVGL 实际提交 139 帧。修复暂停 raster 但保留 LVGL 绑定时的 quiesce 兼容差异，Host/ASan 34/34 及三项各 50 次回归通过。未改默认依赖、未跑官方 benchmark；详情及硬件适配见 [P4 交接结果](test_apps/legacy_compat/RESULTS_C3_P4_HANDOFF.md)。

P4 交接视觉反馈更正：用户报告仅背光亮、无显示。6 轮串口 PASS 仅证明日志层交接，不能标为显示验收通过；C3 保持未通过，需驻留测试固件并同步排查面板输出。详见交接结果的视觉反馈更正。

## 门面的能力超集原则（2026-11-02）

门面以 V1 已成功调用的契约为底线，向上开放已验证的 V2 能力。旧成功调用的逻辑尺寸、像素、所有权和生命周期语义不变；以前失败的配置可以新增支持。PV2_PLAN_V1_DEFAULTS 选择默认模式/池数量，不充当 V1 能力上限。

已移除 GRAM、DIRECT 非零旋转的纯 V1 限制，联动 buffer-count 查询、资源创建、plan 和 set_rotation。初始非方形旋转保持原配置的逻辑宽高，物理输出按角度映射；运行时非方形跨轴仍拒绝，不会静默交换旧调用方的逻辑宽高。GRAM 四方向及同轴运行时二次提交、DIRECT 四方向及同轴二次提交均以真实 V1 几何函数做逐像素 Host 对照。

真实资源限制仍由核心检查：例如 0° 原地字节交换 GRAM 实例不能运行时切换非零角度，失败保留 generation。需旋转的调用方可在创建时指定角度以获得转换路径；本轮不为原来成功的 0° 配置强行增加转换池。TE 平台装配、单 FB、RGB 无 bounce、非方形运行时跨轴等缺口继续明确记录，不冒充超集已全部实现。

本轮仅 Host 与离线编译，不烧录、不改默认示例选择；之前 P4 黑屏/复位观察仍独立待定位。完整结果见 [超集旋转验收](test_apps/legacy_compat/RESULTS_SUPERSET_ROTATION.md)。

V1 取消契约收敛：void cancel 对外立即撤销 frame/lease，内部保留取消排空；下一次 begin 共用准入预算，超时可重试且不提前释放源。独立控制任务只能处理门面已撤销的 producer frame。见 [取消回归](test_apps/legacy_compat/RESULTS_CANCEL_COMPAT.md) 与 [V1 成功路径缺口](V1_COMPATIBILITY_GAPS.md)。本轮不是整体兼容完成，显示黑屏反馈与创建/旋转缺口仍未关闭。
