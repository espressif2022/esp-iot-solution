# P1：模式与能力迁移审计

日期：2026-10-30。状态：第一轮源码盘点完成，目标决策待 P2/P3 review；不是迁移完成证明。

审计对象是当前工作树，包含未提交修改。结论区分公开入口、内部潜在能力、实际测试覆盖。测试文件存在不代表覆盖所有组合。本轮只做文档与现有 Host 基线，未烧录。

## 1. 源码索引

| 标记 | 文件与核对位置 |
| --- | --- |
| V1-C | [配置](../esp_display_present/include/esp_display_present_config.h)：mode、drawbuf、te_compose_buffers |
| V1-P | [profile](../esp_display_present/src/target/present_profile.c)：resolve_mode、validate_profile |
| V1-R | [target resolve](../esp_display_present/src/target/present_target_resolve.c)：默认 tile、内存余量、公开参数校验 |
| V1-F | [Presenter](../esp_display_present/src/esp_display_presenter.c)：presenter_plan_frame、generation、tracking |
| V1-T | [TE timing](../esp_display_present/src/platform/present_te.c)：te_derive_schedule、当前相位准入 |
| V1-X | [TE transport](../esp_display_present/src/producer/stage/present_te_transport.c)：te_wait_submit_window、submit_te_pipeline |
| V1-B | [TE render](../esp_display_present/src/producer/present_te_render.c)、[pool](../esp_display_present/src/producer/present_te_pool.c)：compose 分配与旧图像复用 |
| V1-D | [drawbuf](../esp_display_present/src/runtime/present_drawbuf.c)、[async copy](../esp_display_present/src/producer/present_async_copy.c) |
| V1-S | [FB render](../esp_display_present/src/producer/present_fb_render.c)、[FB fence](../esp_display_present/src/producer/present_fb_fence.c)、[repair](../esp_display_present/src/producer/present_buffer_repair.c) |
| V2-P | [Presenter](src/presenter.c)：resolve_config、等待和后台 service；[接口](include/present_presenter.h) |
| V2-G | [GRAM](src/backend/gram.c)、[SPI](src/platform/lcd_spi.c) |
| V2-T | [TE](src/backend/te.c)、[composer](src/backend/te_composer.c) |
| V2-F | [FB](src/backend/fb.c)、[scanout](src/platform/lcd_scanout.c) |
| V2-X | [pixel engine](src/pixel/pixel_engine.c)、[accel](src/platform/pixel_accel.c)、[内存](src/present_os.c) |
| V2-L | [LVGL](bindings/esp_lv_present_v2/present_lvgl.c) |

## 2. 公开模式逐项对照

“保留”是建议目标，不代表批准或已经实现；各模式共享异常验收 A01～A06。

| ID | 模式 | V1 行为与依据 | V2 行为/差距 | 目标归属与建议 | 验收 |
| --- | --- | --- | --- | --- | --- |
| M01 | NONE / IO | V1-P：GRAM 直传；非零旋转拒绝 | V2-G：多 tile、可旋转；行为扩展 | 区域构建＋GRAM 调度；保留无 TE 路径，旋转列扩展 | test_gram、test_rotation；S31 全屏 band、延迟 DMA 源保护 |
| M02 | NONE / FB | V1-P：persistent 单 FB | V2-F：必须显式允许扫描中写 | 单 FB 资源策略；保留明确撕裂许可，不承诺取消恢复像素 | test_fb/grid；S3/P4 单 FB 独立验收 |
| M03 | DOUBLE_FULL | V1-P：2 FB、FULL | V2-P/F 同预设，事件协议不同 | 完整图像 FULL＋双 FB；保留 | test_fb/grid；S3/P4 绘制/扫描重叠与旧源保护 |
| M04 | TRIPLE_FULL | V1-P：3 FB、FULL | V2-F 有 SCANNING/PENDING/READY，板测曾逐帧等待 | FULL＋3 FB 顺序队列；保留无主动丢帧 | test_fb/pipeline；板测必须证明第三块实际使用和满池背压 |
| M05 | DOUBLE_DIRECT | V1-P/S：0°脏区 surface；旋转拒绝；有历史 repair | V2-F：prefill＋未声明区域恢复；旋转扩展增加逻辑池 | DIRECT 内容有效性独立契约；不能把 prefill 简单删除 | test_fb、test_repair、test_rotation；读旧像素、脏区外保护、写放大测量 |
| M06 | DOUBLE_PARTIAL | V1-P/R：2 FB，独立 1/2 draw tile | V2-F：2 FB，仅一个 scratch、一个 copy job | 完整图像 PARTIAL；tile_count 与 frame_count 分离 | test_fb/asynchronous_partition；新增双 tile 交错及 S3/P4 带宽验证 |
| M07 | TRIPLE_PARTIAL | V1-P/R：3 FB，独立 draw tile 池 | V2-F：3 FB 基线为最新封帧，scratch 仍单个 | 同 M06，增加有序增量依赖 | test_fb/pipeline、test_lvgl_fb；第三缓冲复用不得丢中间增量 |
| M08 | TE_SYNC / 单 compose | V1-C/R/X：0/1 解析为1，push 后等传输完成 | V2-T：固定两张图像，不能选单 compose | 完整图像＋TE；保留低内存串行选择，明确取消破坏基线后全绘 | 新增单 compose Host；S31 全屏内存及串行路径 |
| M09 | TE_SYNC / 双 compose | V1-R 公开入口最大2；V1-X/B 可发送当前帧同时构建下一帧 | V2-T：两张 baseline/candidate，但单活动帧；并不等价 | 完整图像＋TE；恢复跨帧重叠及相位调度 | 新增并行源读/repair/下一帧写 Host；S31 全屏双 compose |
| M10 | AUTO | V1-P：IO 按 te_enabled；FB 默认 TRIPLE_PARTIAL | V2-P：FB 按1/2/3数量；IO 按两项预算选 TE | 配置解析；兼容默认与新显式策略分开命名，禁止静默改变 | test_presenter/test_fb 增加默认值对照，无需靠板测证明解析 |
| M11 | DEFAULT / 扩展组合 | V1 无对应 mode | V2-P 支持原样配置；包括部分非命名 FB 组合 | 扩展单列，不当作 V1 功能；支持矩阵创建时校验 | 所有扩展组合正/负向 Host；支持后再板测 |

**纠正此前泛化描述**：V1 内部 compose 数组上限为3，但 `present_target_resolve.c` 公开创建校验拒绝 `te_compose_buffers > 2`。因此公开迁移基线是单/双 compose；三 compose 是潜在扩展，不能算 V1 已公开支持或本轮必须补齐的旧功能。

## 3. 横向能力与差异

| ID | 能力 | V1 / V2 证据和差异 | 建议决定与归属 | Host / 硬件验收 |
| --- | --- | --- | --- | --- |
| F01 | tile 默认值 | V1-R：按约32KiB选行数；GRAM 默认双块；FB/TE 受核数和128KiB余量影响。V2 要求显式非零配置 | 资源解析公开预算和实际选择；兼容默认单列 | 默认解析、内存不足、不可分配组合；板上记录实际池 |
| F02 | tile 并行 | V1-D 支持1/2块和可选 worker；V2 GRAM/TE 1..16，FB PARTIAL 单 scratch | 统一 tile 池契约，分别配置 frame/tile/tx 数量 | gated copy＋第二块绘制；证明吞吐而非只增加计数 |
| F03 | compose 并行 | V1-X 提交后复用旧 display，下一次 push 仍等待总线 idle；V2 单活动帧 | 恢复双 compose 流水，保留单传输通道约束 | 延迟 DMA 同时构建下一帧；提交拒绝/失败不得写坏源 |
| F04 | TE 相位 | V1-T 有 fast/lead/slow/overrun；V2 为固定 window-budget | 相位作为独立纯策略；输入时序＋扫描模型 | 边界值、抖动、过期、极性、传输跨周期；全屏板测与波形 |
| F05 | TE 失效处理 | V1-X 的等待函数不传递错误：超时后仍可能发送，3次后 free-running；V2 不自动降级 | **明确改变**：默认无合法窗口就暂停/报错，保留源；不照搬静默降级 | 缺边沿/超时不得调用 submit；断开 TE 的受控板测 |
| F06 | 当前周期复用 | V1-T 可使用当前周期剩余相位；V2 offer 要求 edge>=ready_us 且新边沿 | 合法性由相位策略判断，旧边沿不能作无限许可；不能简单保留现限制 | READY 在边沿后、合法晚窗口、边界外拒绝 |
| F07 | PSRAM | V1-D/B 有 PSRAM 分配及内部回退；V2-X 内部 DMA，V2-G SPI 拒绝非内部源 | 内存能力前置；直接 DMA 或显式 staging，禁止只改分配标志 | 范围/cache/暂存源寿命；S31 全屏实际路径与吞吐 |
| F08 | 外部/初始 FB | V1-C/R 可借驱动 FB；V2-F 有 external/initial_scanout | 保留；初始扫描引用先登记，stop 后才归还 | test_fb、test_scanout；S3/P4 初始图像不可覆盖 |
| F09 | 图像基线 | V1-S/B 有 repair 历史；V2 TE 成功基线、FB 最新封帧基线 | 统一图像版本和依赖；允许共享只读源，失败终结依赖链 | 连续增量、前帧失败、基线被引用时禁止写；整图对照 |
| F10 | 首帧与覆盖 | V1-F 协商首帧全绘；V2 composer/FB 检查实际覆盖，GRAM 不保留完整图像 | 保留生产者全绘请求＋构建器实际检查，不能只设 full_redraw | test_coverage/test_te_composer/test_lvgl；不完整首帧 |
| F11 | damage 对齐 | V1-F 返回按 render_alignment 扩展的 render areas；V2 request 无对应协商字段 | 放绘制计划层；决定兼容入口，不能在发送层偷偷扩区域 | 奇数宽、边缘裁剪、对齐溢出、实际绘制区域对照 |
| F12 | scroll | V1-F：PARTITION 明确拒绝；surface 契约 scroll 请求转全绘。V2 无相同请求接口 | 保留可解释退化；不承诺滚动拷贝优化 | PARTIAL 负向、surface 全绘协商和像素参照 |
| F13 | format/stride/swap | 两版有 RGB565/RGB888 等；V2 pixel/GRAM 有严格容量、packing、受控原地翻转 | 像素执行＋内存描述统一；传输格式与渲染格式分清 | test_pixel/test_gram/test_lvgl；奇数宽padding、拒收回滚 |
| F14 | rotation | V1-P 禁止 plain GRAM 和 DIRECT 非0°；V2 扩展四方向，FB 增逻辑池 | 扩展显式声明；排空、全绘、触摸逆变换和内存预算一起定义 | test_rotation；非方形图、四方向实板 |
| F15 | 异步 copy | V1-D 按条件启用/失败回退；V2-X 显式配置、ticket 和执行器 | 共用执行机制，保留源到完成；失败后不能盲目回退重写 | test_repair/test_pixel；延迟读取、取消、执行失败 |
| F16 | 加速器 | V1 有 PPA；V2 有 CPU/DMA2D/PPA，平台限制不同 | executor 插件，不复制图像生命周期；硬件提交前决定回退 | test_accel；P4 与 CPU 对照、实际cache/PSRAM验证 |
| F17 | LVGL | V2-L 有 PARTIAL/FULL/DIRECT；零拷贝限 FULL/RGB565/native/兼容stride/多FB | 独立 Producer 绑定；不将 V2 绑定视作旧应用已无缝迁移 | 四组 LVGL Host；S3/P4真实渲染、S31 TE、源复用 |
| F18 | tracking/generation | V1-F 为 frame_id+generation 与时间线；V2-P 为独立 observer token | 显式兼容决定；取消观察不取消硬件，旧 token 不可复用 | test_presenter；旋转/重绑前后receipt负向 |
| F19 | 完成语义 | V1-S 分 transfer/present/confirmed；V2-F adapter 两边界确认，TE 只有传输 | 区分借用结束、图像激活、旧FB退役；平台证明不能全局套用 | test_scanout；S3/P4单次提交后事件序列 |
| F20 | RGB 非bounce | V2 adapter 明确要求 bounce；不能从 V1 API 类型推断所有硬件支持 | 有退役证明才开放；能力矩阵明确拒绝 | 创建负向；若后续开放需独立驱动审计和板测 |
| F21 | 调度/线程 | V1 分散 worker/等待；V2 owner+可选 sender，GRAM sender要求异步copy | 公共推进，ISR只记事实唤醒；双等待域不互吞 | test_scheduler/test_event；队列满、唤醒交错、stop |
| F22 | 取消/停止 | V2 有排空、closing重试与active callback计数；语义分散三后端 | 公共运行核心；单compose/单FB的像素回滚不做虚假承诺 | test_repair/test_scanout/test_records；板上stop/recreate |
| F23 | 接受性与故障 | V2 区分 REJECTED/ACCEPTED/UNCERTAIN；SPI 部分入队可能不确定 | 保留；确定拒收可重试，已接受失败sticky，禁止提前释放 | test_lcd_spi/test_te/test_fb；Host故障注入为主 |
| F24 | 多实例/producer交接 | V2-P owner绑定、显式handoff；binding另有线程归属 | 保留单producer边界；实例token/事件/资源隔离 | test_presenter/test_scheduler；交接及旧调用拒绝 |

## 4. 必须先定的架构决策

| 决策 | 推荐方向 | 为什么阻止直接补代码 |
| --- | --- | --- |
| D01 | tile_count / image_count / tx_capacity 独立，统一资源账本 | 否则 FB 双tile、TE 双compose会继续挤进含义不同的 buffer_count |
| D02 | 帧结果、图像版本、物理分配三种身份分离 | receipt终态不代表可写；同一图像可作为基线和输出源 |
| D03 | TE/FB 共享完整图像构建，GRAM保持流式区域路径 | 封帧不是GRAM开始输出的前提，不能套万能整帧pipeline |
| D04 | 输出策略只消费就绪图像/块、更新输出引用 | 不再各自持有另一套record、cancel、deadline |
| D05 | 保留低内存单compose，但取消后允许基线失效要求全绘 | 无备用完整图像就无法保证像素回滚 |
| D06 | 新API明确语义；V1默认值/计划协商兼容层显式决策 | 不应借“新架构”静默丢弃用户依赖行为 |
| D07 | TE 没有合法窗口不自动free-running | 这是有意改变旧行为，必须公开说明与负向验证 |

## 5. 验收分层与板子分工

公共异常组：A01早到/迟到完成；A02明确拒收/不确定接受；A03零/有限超时不释放源；A04取消与stop重试；A05无receipt回收/观察回收；A06实例隔离、旧token与事件溢出。每种输出策略都跑，不能只在一个后端验证后宣称全覆盖。

Host 必须覆盖所有命名模式及承诺的扩展配置，完整像素对照、源延迟读取、第三缓冲真实使用、真实 LVGL。普通与 ASan/UBSan 分开记录；关闭 LeakSanitizer 时不声称泄漏检测通过。

| 板子（历史身份，执行前重新识别） | 必须实际验收 | 不能替代的证明 |
| --- | --- | --- |
| S3 RGB 480×480，MAC 90:e5:b1:d0:a9:d8，曾 USB0 | FB 五模式、真实队列背压、LVGL、旋转、停止重建 | 不能替代 MIPI 退役协议或 QSPI TE |
| P4 MIPI 1024×600，MAC e8:f6:0a:e7:a7:8d，曾 ACM0 | 同上，加 PPA/DMA2D 与外部内存路径 | 不能用两次边界计数证明所有驱动均安全 |
| S31 QSPI 480×480，MAC 30:ed:a0:f4:0b:34，曾 ACM1 | 全屏GRAM、TE单/双compose、动态相位、实际绘制/发送重叠、PSRAM或staging | 64×16 ROI与目视正常不证明全屏TE无撕裂 |

用户已要求需要的测试上板；本轮无生产变更，暂不重烧同一固件。P3设计review后按迁移切片安排板测，不等全部代码写完才发现内存/时序无法实现。每次写入记录MAC、固件hash、模式、配置、日志和视觉结果；端口号不能单独作为身份。

## 6. 当前完成与未完成

本轮已核对公开模式解析、TE公开/内部数量差异、动态窗口、超时退化、绘制池、内存准入、FB scratch、scroll/对齐协商、tracking接口与既有测试入口。未做V1所有并发路径的正确性证明，也未做新架构实现。

P1输出可进入P2；D01～D07保持待review，不伪装成已批准决定。V1应用绑定逐调用兼容、平台驱动版本证明和每个Host用例的精确需求映射还需在P3/P6细化，不能因此把对应能力标为迁移完成。

## 7. 本轮基线实跑

当前源码重新构建后，普通Host+LVGL 21/21、ASan/UBSan 21/21通过（detect_leaks=0，未验收泄漏检测）。日志位于忽略目录 `test_apps/boards/artifacts/p1-baseline-host.log` 与 `p1-baseline-sanitize.log`。测试仅验证现有实现；上表新增验收条件仍待实现后执行，不能因基线通过而关闭差距。
