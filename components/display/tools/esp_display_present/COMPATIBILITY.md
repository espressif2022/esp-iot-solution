> 当前布局以 [DEFAULT_SWITCH.md](../esp_display_present_v2/DEFAULT_SWITCH.md) 为准。下文 C1/C2 和旧测试数量保留阶段性记录。

# 旧 API 门面（V2 默认实现）

当前目录已增加 V1 同名函数和 IDF 组件选择入口；`src/resources.c` 是其私有资源装配，不是另一套应用 API。当前只对下列支持范围完成基础 Host 验收，**默认入口已切到 V2；支持范围及拒绝项见 DEFAULT_SWITCH.md，不宣称覆盖所有旧驱动配置**。`include/` 保留原 V1 四份公共头文件（含 geometry），不改字段、枚举和布局。旧 LVGL 入口在相邻 `../esp_lv_present`，连接 V2 的安全绑定。

## 当前已实现

- 按旧配置归一化 MIPI、已确认的 RGB bounce 和 SPI/GRAM；AUTO 的 FB 默认三缓冲 PARTITION，IO 无 TE 时为 GRAM。
- 默认约 32 KiB/块、两块 tile（显式 lines/buffers 优先），FB 后台 service 和异步 copy；FULL/DIRECT 不分配 tile。
- 创建 CPU 执行器，所有可失败的框架分配/回调 attach 成功后再尝试可选 DMA2D/PPA。0° 优先 DMA2D，旋转优先 PPA，缺少硬件时 CPU；创建结果可查询，不假装已经使用硬件。
- 执行前仍走公共 probe/selector；执行器开始后出错不重试 CPU。统计是实际选中执行器的尝试次数，包含执行失败，不是成功 DMA 数。
- 获取精确驱动 FB 池并借用，校验旧配置显式传入的地址/数量；不接管或删除 panel、IO、bus。
- 驱动可能仍在读旧实例最后一张 FB。首次 producer lease 前先显式提交 FB0，并通过原双边界 fence 确认；创建本身不发送像素。prepare 超时保留实例，重试不重复提交 bootstrap。
- 最终 destroy：停止/加入 sender 和 pixel engine，排空待切换帧，摘除回调并做 ISR join；仅驱动自有、不会被框架释放的外部 FB 可以继续不可变扫描。再销毁 backend、adapter、执行器和资源容器；每个阶段只在成功后清空句柄，错误保留重试。
- 保留原底层适配器的默认行为；retain_driver_scanout 只用于明确的外部驱动池，不把“背光关闭”当成扫描停止。

调用顺序（仅门面内部）：

```c
pv2_legacy_resources_create(&legacy_config, rgb_bounce_confirmed,
                            on_vsync, vsync_user, &resources);
// 每步检查返回值。首次准备可能 TIMEOUT，保留 resources 再试。
pv2_legacy_resources_prepare(resources, timeout_ms);
// prepare 成功前只可读配置/caps，不得 begin/acquire 或让 LVGL 绘制。
pv2_presenter_t *core = pv2_legacy_resources_presenter(resources);
// ...既有 producer / drain / handoff...
pv2_legacy_resources_destroy(resources, timeout_ms);
// 成功后 panel/IO 仍由其原 owner 持有。
```

`pv2_legacy_resources_select_executor()` 只更新执行器偏好，不旋转图像。门面仍须通过引擎排空并更新旋转；两个插件都存活到引擎停止，不会在切换偏好时注销。

最终 destroy 前，producer 必须已停止写入并取消未封帧的 lease；C2 不能把控制线程 stop 直接转发成强制释放。普通 presenter drain（未来 V1 quiesce）不走最终 detach 路径，可继续显示/渲染；destroy 进入 closing 后只允许重试销毁。不要把两个动作合并。

## 当前支持边界

最新逐项状态见 [兼容状态](../esp_display_present_v2/V1_COMPATIBILITY_GAPS.md)。TE 已接入测量式准入 gate、GPIO/定时器和安全 DMA staging；未测得窗口或整屏发送超窗时等待并超时，不降级成无同步发送。S31 通过 SPI 只读事实确认 direct-PSRAM，避免重复 staging 后，单/双 compose 与旋转传输通过；未知 IO 仍保守 staging。

MODE_NONE 对 RGB/MIPI DSI 使用 PARTITION：present 只管理 tile，`draw_bitmap()` 将脏区复制到驱动内部 framebuffer，`on_color_trans_done` 后释放 source；显式 FULL/DIRECT 模式仍使用 scanout framebuffer 池。FB swap、非 64B 有效容量和 SPI PSRAM 绘制池不再一律拒绝。运行时旋转保持 V1 逻辑尺寸，原生默认旋转契约不变。常规 coverage 只保留有界精确矩形，碎片化才惰性分配位图。

RGB 优先使用 [公开只读 IDF 查询](../esp_display_present_v2/test_apps/driver_patches/README.md)，不要求旧应用增加调用。未修补 SDK 的显式确认仍保留。连续无 bounce 多 FB 的 S3 和 refresh-on-demand 未获得安全退休适配，继续拒绝。驱动事实不匹配和非法参数仍拒绝。

## 生命周期证据与局限

Host 新增 legacy_resources，覆盖配置、创建前拒绝、attach 失败回收、CPU/模拟硬件选择、执行失败不 CPU 重试、初始源建立与重试、保留扫描、重复创建及 adapter/插件销毁失败重试；SPI 测试覆盖原地交换和在途 stop 超时。硬件插件本身仍由现有四种 executor 构建组合验收，资源聚合测试不冒充 GPU 实测。

scanout 测试额外覆盖：原始 VSYNC 不退休缓冲、关闭期间等待已接受切换、取消注册失败、已抓取旧 user_ctx 但尚未进入的 ISR 模型。Host 模型不证明真实双核 IRQ 时序；IDF 使用取消注册后的跨核 IPC join。

IPC 与原生 panel delete 一样，底层同步 API 不提供硬超时。框架在原生 join 前后检查剩余预算，超时保留对象；不能宣称对卡死 IRQ 有硬 deadline。zero-timeout 在真实多核上可能需要后续非零预算才能完成 join。回调必须 ISR-safe、非阻塞，不能重入 task API。

初始 FB0 建立可能短暂显示该 FB 的旧内容，随后要求生产者首帧全绘；正常同实例 handoff 不重新 bootstrap。没有删除/重建 panel，也没有保证删除 presenter 后重建的视觉连续性。

`status.plan` 是创建时核心池规划，加上资源容器的 reservation；不包含 SDK driver/GPU/allocator 的实测开销，不是全堆占用测量。`preferred_executor` 是偏好，实际硬件/CPU 次数以执行统计为准。

C1 时点尚无同名入口；C2 基础入口现已实现，详见下文。旧板测仍不覆盖新的 borrowed-stop 路径。

本切片验收：Host/LVGL 32/32，ASan/UBSan/LSan 32/32（detect_leaks=1）；scanout 与 legacy_resources 各连续 30 次通过；S3/P4 离线 smoke 强制链接整个资源层及 SDK 执行器入口通过。未烧录、未执行真实 borrowed-stop / IPC 时序测试。

## C2 接入与边界

选择本目录替代原 `esp_display_present`，选择相邻 `../esp_lv_present` 替代原 LVGL 组件，并加入 V2 核心和 binding。两个同名实现不可同时作为同一 IDF 组件；V2 核心没有 V1 导出符号。可复现的原工程副本见 [legacy_compat](../esp_display_present_v2/test_apps/legacy_compat/README.md)。暂不建议直接切换产品默认依赖。

首帧/取消后/LVGL 交接后协商 full=true/count=0；PARTITION 拒绝 scroll，FULL/DIRECT 对合法 scroll 请求完整 raster。所有 dirty/scroll 参数先校验，合法矩形再按旧 inclusive 坐标对齐扩展并裁边。不支持的 TE/单 FB 等创建仍明确拒绝。RGB 需应用明确确认 bounce 配置并启用 `CONFIG_PV2_LEGACY_RGB_BOUNCE_CONFIRMED`，不能从 opaque panel 句柄猜测。

门面 task API 由自己的 mutex 串行；删除必须与所有 API 调用互斥。控制任务查询可与完成事件并发；控制 stop/destroy 前先停止 producer，held lease 仍由 producer cancel。所有超时保持 closing 对象/缓冲可重试，不允许通过 rebind 或 rotation 清除故障。短 stop 预算下线程 join 可能需要重试。

tracking 的两个后续 VSYNC 从任务观察到成功 fence 时开始计，可能比精确 ISR fence 多等待。取消显式注销观察与确认等待，不触碰后端仍持有的 pending receipt/硬件缓冲。公开重复查询读取累计时间线，不会因查询而丢失已发布的结果。

**仍待收敛**：TE/单 FB 等平台事实与事件源、非方形运行时跨轴旧契约、三板及实际 GSP 运行；showcase 原 1.5 发布组合仍未验证。初始非方形四方向已经与 V1 纯几何实现做 Host 差分；运行时跨轴不能静默交换旧逻辑宽高，因此门面明确拒绝。

## C2 第二轮的统一期限策略

旧配置 transfer_timeout_ms=0 仍按 1000 ms，pipeline_acquire_timeout_ms 独立控制准入。原生 V2 新可选 transfer_timeout_ms 默认 0 关闭；在既有 Presenter 的真实 panel.submit/完成入口逐笔计时，不再观察整个封帧队列是否停滞。计时元数据不持有源引用；只在启用时分配并计入核心预算。实际 source release 仍由后端/驱动证明。

超时发布 sticky FAULT，迟到 DONE/SCANOUT_CONFIRMED 仍转交真实完成；明确拒收不置 fault，不确定接受不撤销引用。已有 sender 按最近期限唤醒，不新增 task；同步无 sender 的 GRAM 在下一次 pump/查询/等待或完成入口检查，不另起空闲 timer，不能打断永久阻塞的驱动调用。首次 FB0 bootstrap 仍由 prepare 的独立预算负责。

完整新结果、V1 band/scroll/full/水位契约和未关闭项见 [第二轮验收](../esp_display_present_v2/test_apps/legacy_compat/RESULTS_C2_CONTRACTS.md)。普通产品默认依赖仍未切换。

## 门面的能力超集原则（2026-11-02）

门面以 V1 已成功调用的契约为底线，向上开放已验证的 V2 能力。旧成功调用的逻辑尺寸、像素、所有权和生命周期语义不变；以前失败的配置可以新增支持。PV2_PLAN_V1_DEFAULTS 选择默认模式/池数量，不充当 V1 能力上限。

已移除 GRAM、DIRECT 非零旋转的纯 V1 限制，联动 buffer-count 查询、资源创建、plan 和 set_rotation。初始非方形旋转保持原配置的逻辑宽高，物理输出按角度映射；运行时非方形跨轴仍拒绝，不会静默交换旧调用方的逻辑宽高。GRAM 四方向及同轴运行时二次提交、DIRECT 四方向及同轴二次提交均以真实 V1 几何函数做逐像素 Host 对照。

真实资源限制仍由核心检查：例如 0° 原地字节交换 GRAM 实例不能运行时切换非零角度，失败保留 generation。需旋转的调用方可在创建时指定角度以获得转换路径；本轮不为原来成功的 0° 配置强行增加转换池。TE 平台装配、单 FB、RGB 无 bounce、非方形运行时跨轴等缺口继续明确记录，不冒充超集已全部实现。

本轮仅 Host 与离线编译，不烧录、不改默认示例选择；之前 P4 黑屏/复位观察仍独立待定位。完整结果见 [超集旋转验收](../esp_display_present_v2/test_apps/legacy_compat/RESULTS_SUPERSET_ROTATION.md)。

## void cancel 的逻辑撤销与实际排空

cancel 立即使旧 frame/lease 身份失效；pending copy/transfer 继续持有真实源。后续 begin 在同一准入预算内完成排空，超时可重试，不返回因残留门面 active 产生的 INVALID_STATE。控制任务 quiesce/交接只可在逻辑帧已撤销后接续排空；最终 stop 超时仍保留资源。完整回归和仍有的成功路径缺口见 [取消结果](../esp_display_present_v2/test_apps/legacy_compat/RESULTS_CANCEL_COMPAT.md) 与 [兼容清单](../esp_display_present_v2/V1_COMPATIBILITY_GAPS.md)。

## 2026-11-03 Review 修复

兼容组件独立持有 `src/present_te.c` 和 `src/present_geometry.c`；前者由原 V1 测量式时序代码演进为非阻塞 gate，后者保持公开 geometry 行为。原 V1 文件不修改，两种同名组件仍互斥选择。V2 的 CMake 与 Host 测试不再编译或包含兄弟 V1 目录的私有实现。公开头文件依赖的 `esp_lcd` 显式列入 REQUIRES。

TE gate 每次只检查当前测量窗口与 after-edge，不保留会被重试覆盖的 frame-request 时间戳，也不提供旧信号量等待路径。GPIO 两个边沿均通知推进；窗口起点尚未到达则挂 phase timer。定时器创建失败明确返回错误，旧 SDK 缺少安全 timer 支持时不伪造推进。剩余预算传给 GPIO join 与 timer drain；IDF IPC 本身仍不保证硬 deadline。

stop 继续拒绝外部持有 lease 或绑定 LVGL 的实例；无 lease 的未封帧构建先取消排空，再停止资源。TE_CANCELING 正常发布取消结果，不制造 PRODUCER_PROTOCOL fault。超时保留访问中的资源，可重试。

自动双 tile 创建遇 NO_MEM 可重新 resolve 为单 tile，仅重试一次；显式两块不降级。规划优先保留能容纳一块 tile 的 PSRAM，并预留内部 compose、staging 和异步 GRAM 转换槽开销。Host 注入真实像素分配失败，不把主机分配器当作真实 ESP 堆碎片模拟。

独立消费工程：`test_apps/legacy_compat/compile`。main 只依赖兼容组件，包含公开 geometry 头并链接符号；不加入 V1 源路径，不依赖业务工程间接补头文件。用于离线构建，不是板测固件。
