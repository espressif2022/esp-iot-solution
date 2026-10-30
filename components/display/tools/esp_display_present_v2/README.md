# Display Present 第二版

本目录独立实现显示框架的新设计，让一次提交、一次完成和一次失败都能在所属后端内追踪清楚。第一阶段提供 CPU 像素引擎与 GRAM 后端，现有 `../esp_display_present` 继续作为行为对照。

接口目前是实验性接口。已有 Host 构建与 ESP-IDF 组件注册；已新增 ESP LCD SPI/QSPI adapter；已新增 GRAM／TE／FB Presenter 语义迁移入口及可选 LVGL 9.5 PARTIAL／FULL／DIRECT 绑定；已增加显式泵送的 CPU TE 后端，并已补齐 DSI／RGB bounce adapter、DMA2D／PPA executor、运行时旋转、显式 tracking 与可选 FULL 零拷贝；尚未提供旧 API 的二进制兼容。S3 RGB／P4 MIPI 已完成 CPU FULL／DIRECT／PARTIAL、双／三缓冲配置及停止重建的基线验收；三缓冲排队背压、LVGL 和其他能力仍待逐项验证。不能作为原组件的直接替换，也没有修改现有产品固件的构建依赖。

独立上板工程与实测记录：[boards/README.md](test_apps/boards/README.md)、[boards/RESULTS.md](test_apps/boards/RESULTS.md)。

## 当前实现与命令

| 已实现 | 范围 |
| --- | --- |
| CPU 像素引擎 | RGB565/RGB888、四方向旋转、RGB565 字节序转换（含安全原地转换）、源只读/重叠校验、带 padding 的 stride、容量及溢出校验 |
| 像素任务记录 | 可选 FIFO worker、事件等待、排队取消/运行中排空、有限记录、跨实例和过期 token 拒绝、查询/等待/释放、`on_event` 完成回调通知；区分关闭态 (`INVALID_STATE`) 与忙/满背压 (`NO_MEM`) |
| GRAM 后端 | tile lease（支持预设区域或 `NULL` 区域 + `submit_region_ex` 子区域/自定义 stride 紧凑化）、可选原地字节序翻转及拒绝时自动回滚、多块帧、可选/强制多帧 receipt、早到/迟到回调、不确定接受隔离、取消超时后台自愈与停止 |
| SPI/QSPI adapter | `draw_bitmap` 与 IO 回调接入、FIFO cookie、DMA 前 cache 同步、部分接受报错隔离、排空与回调退出屏障；目前仅离线验证 |
| Presenter | GRAM/TE/FB 选择与模式预设（默认 GRAM）、单 producer、有界准入等待、调用序列/lease 校验、有限 receipt 自动回收、非破坏性 drain、取消/停止与错误状态 |
| 可选 LVGL 绑定 | 真实 LVGL 9.5 PARTIAL/FULL/DIRECT、独立绘制缓冲、按后端容量拆 band、RGB565/RGB888、错误暂停、多显示实例 |
| TE 后端 | 同步 CPU / 异步 FIFO copy、COPY_DRAIN、分块合成、首帧覆盖、基线补齐、窗口准入、panel 回调、receipt、fault/stop；显式 service 或可选 Presenter 后台任务，已接真实 LVGL |
| FB CPU 后端 | 单/双/三 FB、PARTITION/FULL/DIRECT、外部 FB、最新封帧基线、顺序切换与旧帧退役；仅模拟 scanout 验证 |
| 平台基础 | Host pthread 与 ESP-IDF 临界区、内部 64B 对齐 DMA-capable buffer 分配、单调时间 |
| 验证入口 | CMake/CTest、整份生产源文件链接、C++ 头文件与链接、ESP-IDF loopback smoke 应用 |

统一入口为 `tools/dev.py`，不再从源码中截取函数片段生成测试代码。

```sh
cd components/display/tools/esp_display_present_v2
python3 tools/dev.py test
```

其他操作使用同一个入口：

```sh
python3 tools/dev.py build
python3 tools/dev.py test --sanitize
python3 tools/dev.py test --with-lvgl
# 先通过本机 ESP-IDF 的 export.sh 激活环境，再交叉编译：
python3 tools/dev.py idf-build --target esp32s3
python3 tools/dev.py idf-build --target esp32s3 --with-lvgl
```

`--with-lvgl` 查找仓库与本地 ComponentManager 缓存中的 LVGL 9.5 源码，不下载；其他位置用 `--lvgl-dir /path/to/lvgl`。核心构建不依赖 LVGL，启用后才编译可选绑定及真实渲染测试。

默认输出在本目录 `build/`，可用 `--build-dir` 指定位置。`idf-build` 只构建 loopback smoke 固件，不连接或烧录设备。不要将该 smoke 固件当成屏幕应用。

如果受 ptrace 限制的环境中 LeakSanitizer 报不支持，可显式运行 `ASAN_OPTIONS=detect_leaks=0 python3 tools/dev.py test --sanitize`；此时仍检查越界和未定义行为，但不代表通过泄漏检测。

接口与实现统一使用 `pv2_` 前缀，完整后端接口描述为 `pv2_gram_backend`、`pv2_te_backend` 和 `pv2_fb_backend`。frame、lease、receipt、copy ticket 是不同类型的句柄，cookie 只用于硬件事务匹配。公共类型在 `include/`，后端私有状态定义在所属 `.c` 文件内。

Presenter 的准入、acquire、cancel、drain 已使用保留通知的事件等待，等待时释放任务互斥锁；TE/FB 可选后台发送。裸 backend/pixel/SPI adapter 的部分 wait/quiesce 仍为短周期轮询，并未全库替换。ISR 只推进状态并通知，不发送。实际唤醒延迟和 CPU 开销仍需硬件验证。CPU 拷贝不持有自旋锁；GRAM 绘制 buffer 采用内部 DMA 内存，SPI adapter 已实现内部 DMA 源的 cache writeback；DSI/RGB 支持驱动拥有的 PSRAM FB；M2M cache 交接见本文末尾执行器契约，实机一致性尚未验证。

2026-10-30 当前验证为 **16 组核心 + 4 组真实 LVGL，共 20 组**。普通 Host 与 ASan/UBSan（detect_leaks=0）通过；P4 + LVGL 离线编译通过。核心新增 repair、rotation、scanout、accel，LVGL 新增 lvgl_zero。测试链接生产源文件，硬件通过驱动 stub 模拟；没有连接或烧录设备，未验证真实 DMA、cache 一致性、屏幕时序或性能。详细支持范围见 PLAN.md 与 review.md。

## TE 当前实现边界

`src/backend/te_composer.c` 是完整 TE 后端的内部 CPU 参考核心，不对外增加另一套 backend/stage 表。它只允许一帧构建或发送，拥有两张内部 DMA 完整帧和每像素一 bit 的覆盖图；内存约为 `2 × width × height × bpp + ceil(width × height / 8)`，另加少量状态。支持旋转与异步 copy/repair；TE 合成缓冲仍为内部内存且只允许一个活动帧。

首帧必须由实际成功写入覆盖全屏，不能用 full_redraw 提示替代；后续封帧只从上一张成功发送帧补齐未写入的行内片段。失败或取消的 candidate 不会变成基线。零更新帧允许重复发送上一张成功画面；每次发送都是整帧，可覆盖此前失败传输造成的部分屏幕更新。

`offer` 只在任务上下文检查新边沿与窗口并借出只读帧，不操作硬件。返回后即固定缓冲，明确未接受时才可 reject，安全完成/中止后才可 complete。重试必须等更新的边沿。窗口检查仅代表可开始发送，真实无撕裂还需板级扫描相位、传输时间预算及调度延迟验证。`te.c` 现已接入 panel 回调、receipt 与停止生命周期。backend 创建时需要设置 `te_window_us` 和 `te_transfer_budget_us`（均非零且预算小于窗口），实际准入预留后者；未自动推导硬件时序。GRAM 忽略这两个字段。

TE 使用 `pv2_te_backend`，调用顺序与 GRAM 一致。end 后调用方须持续执行 `pv2_te_service`；query/wait/begin 也会泵送。ISR 通过 on_event_isr 提交同一单调时钟域的 TE_EDGE，仅记录最新事件，不发送。首版只允许一个活动合成/发送帧，未完成时 begin 返回 NO_MEM；完成 receipt 可继续保留到显式释放。明确拒收由 service 返回错误并保留 READY，等待新边沿重试；query/wait 观察 receipt，拒收期间仍为 pending。end 成功消费 frame，不以之后的驱动拒收伪装成 end 回滚。

TE 已接入 Presenter／LVGL，可选 background_service 后台调度。销毁前，应用还必须注销外部 TE GPIO/Timer 事件源并等待回调退出；panel.quiesce 只覆盖它自己的回调。三个后端的 sticky fault 都会阻止新帧准入，恢复须安全停止并重建。

## Presenter／LVGL 的 TE 使用方式

`pv2_presenter_config_t.backend_kind` 默认为 `PV2_PRESENTER_GRAM`；选择 `PV2_PRESENTER_TE` 时，backend 配置仍须填写经过测量的 TE 安全窗口和整帧传输预算。示例中的 `backend_config` 已包含 panel adapter、尺寸、像素格式、缓冲数及这些预算，不提供通用于所有屏的固定数值。

```c
pv2_presenter_config_t config = {
    .backend = backend_config,
    .timeout_ms = 50, /* 本次帧准入的总等待预算，按应用需求配置 */
    .backend_kind = PV2_PRESENTER_TE,
};
pv2_presenter_t *presenter;
ESP_ERROR_CHECK(pv2_presenter_create(&config, &presenter));
pv2_lvgl_t *binding;
ESP_ERROR_CHECK(pv2_lvgl_create(presenter, 4, &binding));
```

默认合作模式下，创建任务的应用循环持续调用 `pv2_presenter_service(presenter)` 和 `lv_timer_handler()`；开启 background_service 后只需继续驱动 LVGL，service 可用于显式重试，并检查 `pv2_lvgl_get_error(binding)`。service 的明确拒收错误不是 fatal：用 get_status 检查 first_error；若仍为 ESP_OK，保留帧并等待下一 TE 边沿重试，不对 service 无条件使用 ESP_ERROR_CHECK。若 first_error 非零或 binding 已记录错误，停止刷新并按既有关闭流程收尾。本轮未增加自动恢复 API。

独立事件任务调用 `pv2_presenter_notify_te(presenter, timestamp_us)`；真实 ISR 使用 `pv2_presenter_notify_te_isr` 并透传返回的 yield 提示。时间戳须与后端单调微秒时钟一致，ESP 上使用 esp_timer 的时间域；通知只记录边沿。GRAM 忽略两种通知。模拟 Host 的边沿/完成由独立线程提供：不要把唯一事件来源放在可能被 begin 阻塞的 UI 循环里。

begin 在 receipt 满或 TE 前帧未完成时共用一个 timeout_ms 预算等待。0 为不等待，超时不新建 frame、不取消前帧、不记录 sticky fault。LVGL 收到超时会记录错误并暂停刷新，不自动丢帧重试。绑定创建时将活动屏幕全屏失效，首次实际覆盖整屏后才能成为 TE 基线；后续刷新由合成核心补齐未修改区域。

销毁时先注销外部 TE 事件源并等待其回调退出，随后销毁 binding、成功 stop/destroy Presenter、最后释放 panel adapter。binding 可在已封帧的发送完成前销毁，因为底层访问的是独立合成/lease 内存；Presenter stop 超时则保持对象和 adapter 有效，待传输完成后重试。

## 模式矩阵与 FB 接入

`pv2_presenter_config_t.mode` 为新 API 的模式预设，不复用旧枚举数值。默认 `PV2_MODE_DEFAULT=0` 保持此前 backend_kind/config 配置。FB 使用 `PV2_PRESENTER_FB`，需提供满足扫描切换契约的 panel adapter；本轮没有真实 RGB/DSI 驱动接线。

| mode | 路径 | 缓冲与 LVGL 模式 |
| --- | --- | --- |
| NONE | IO → GRAM；FB → 单 FB | PARTIAL；单 FB 必须显式 allow_active_scanout_writes |
| DOUBLE_FULL | FB | 双 FB + LVGL FULL |
| TRIPLE_FULL | FB | 三 FB + LVGL FULL |
| DOUBLE_DIRECT | FB | 双 FB + LVGL DIRECT |
| DOUBLE_PARTIAL | FB | 双 FB + LVGL PARTIAL |
| TRIPLE_PARTIAL | FB | 三 FB + LVGL PARTIAL |
| TE_SYNC | IO → TE | 两张内部合成帧 + scratch，LVGL PARTIAL |
| AUTO | 根据显式 backend_kind/config | FB 2/3 → 相应 PARTIAL，FB 1 → NONE；IO 配有两项 TE 预算 → TE，否则 GRAM |

AUTO 不探测物理 panel 类型、不猜测 TE 时序。FB 模式预设要求 backend_kind=FB；其他 backend 使用这些模式会返回 NOT_SUPPORTED。模式预设设置 FB 数目；external_buffers 存在时原 buffer_count 必须匹配预设数目，防止越界读取数组。DEFAULT 可直接指定 FB render_contract 和 1..3 个缓冲。FULL/DIRECT 要求 draw_order==panel_order，三个后端支持四方向旋转；FB 转换需额外逻辑绘制池。

```c
pv2_presenter_config_t config = {
    .backend = fb_config, /* 尺寸、格式、tile_rows、max_frames 和 scanout adapter */
    .backend_kind = PV2_PRESENTER_FB,
    .mode = PV2_MODE_TRIPLE_PARTIAL,
    .timeout_ms = 50,
};
```

FB 分配两/三张整帧（或借用相同数量的 external_buffers）；PARTITION 额外分配一块 scratch，FULL/DIRECT 直接把完整 candidate 作为 lease。外部缓冲需 64B 对齐、足够容量且互不重叠，在成功 stop 前由后端独占，不会由后端释放。内部缓冲为紧凑 stride；外部额外容量不表示支持自定义扫描 stride。

只有 `SCANOUT_CONFIRMED(cookie)` 同时证明新 FB 生效、旧 FB 不再被读，才可归还旧扫描源。TRANSFER_DONE 或未经关联的 VSYNC 无此含义。receipt 成功可立即回收，但当前 FB 继续由扫描器借用；只有新 lease 才授予写权限。stop 即使没有待完成 receipt，也必须等 adapter.quiesce 真正停止持续扫描及相关回调。

三缓冲允许“当前扫描 + 已提交切换 + 构建/待提交”重叠，不丢中间帧；只保留一笔驱动切换，READY 按封帧顺序发送。PARTITION/DIRECT 从最新封闭且不可变的候选帧建立 CPU 基线，保证排队的增量修改不会丢失；前序失败后整个后端 fault，未发送的队列终结为失败，不继续显示依赖失败前帧的增量画面。首帧要求提交区域实际覆盖全屏，FULL 每帧都要求全屏；DIRECT 封帧时恢复未声明的区域，防止未提交脏数据成为新基线。

单 FB 的 NONE 是明确的可撕裂模式：写入可能立即可见，取消不能还原已显示像素，取消后下一帧要求全覆盖。未开启 allow_active_scanout_writes 时拒绝创建单 FB，绝不冒充双缓冲隔离。

FB 同样使用创建任务的 Presenter service（裸 backend 用 pv2_fb_service）；query/wait/begin 也推进状态。LVGL FULL/DIRECT 将 draw_rows 忽略并分配整屏绘制源，PARTIAL 继续按 draw_rows 分块。可选择 Presenter 后台线程；另有 opt-in FULL 零拷贝和可选硬件 executor，见文末限制。Host 模拟扫描通过不等于真实 panel 无撕裂。

## 可选后台调度与事件等待

在 TE/FB 的 Presenter 配置中设置 `.background_service = true`。默认为 false，保留既有显式 service；GRAM 在 `backend.async_copy=true` 时也支持后台 sender；同步 GRAM 仍拒绝该设置。Host 创建 pthread；FreeRTOS 默认栈 4096 字节、优先级 5，可用 worker_stack_bytes / worker_priority 覆盖（0 取默认，Host 忽略这两项）。建议为可能等待前帧的 UI 配置非零 timeout_ms；0 始终是立即检查，不保证在后台临界操作期间准入成功。

提交封帧、TE 边沿和 panel 完成/故障都会唤醒 sender。后台只调用后端 service，不执行 LVGL 或回收 Presenter 的 receipt 队列；receipt 仍由创建任务查询/回收。Presenter 用一把任务互斥锁串行化所有后端任务侧操作，CPU 绘制已借出的 lease 时，后台只能操作其它受状态机保护的缓冲。

等待使用两个独立的保留型通知：owner 的 changed 与 sender 的 work。信号先到也不会丢失，多次信号合并后必须重查真实状态；sender 不消费 owner 的通知。Host 用 CLOCK_MONOTONIC 条件变量，FreeRTOS 用静态二进制信号量，ISR 信号向上传递 need_yield。Presenter 等待时释放任务互斥锁；恢复后重新获取并检查条件，整个准入/排空共享原超时预算，不逐次重置。

last_service_error 暴露最近一次显式/后台 pump 的结果，明确拒收不会写入 sticky first_error。TE 拒收等待新边沿；FB 拒收不会后台无休止自重试，可由用户显式 service 或后续事件触发重试。进入故障态后仍需按停止流程收尾。

stop 先请求并等待 sender 退出，再调用 backend.stop / adapter.quiesce，两段使用同一剩余预算。sender 已在驱动 submit 中时，join 超时保留整个对象并拒绝新 begin，禁止在另一线程同时 quiesce；释放驱动后可重试 stop。destroy 在成功 stop 前立即拒绝。adapter.submit 本身须及时返回：等待后为安全读取状态重新获取任务锁时，不能对任意阻塞驱动承诺硬实时上限。外部 TE 事件源仍须在 destroy 前注销并等待回调退出。

这轮只替换 Presenter 等待路径；直接调用裸后端仍遵守单任务串行契约，不会自动产生后台任务。异步像素 copy、DSI／RGB bounce adapter 已实现；实际接线和硬件时序仍未验证。

## 设计结论

采用三个后端：GRAM 分块传输、TE 整帧合成、FB 扫描缓冲。每个后端完整负责自身资源、提交、回调、回滚、停止和销毁。Presenter 负责公共调用契约，像素引擎负责拷贝、旋转、字节序转换及相关硬件完成。

不再通过可组合的 begin、repair、commit stage 拼出一条显示流程。后端内部用普通函数表达固定流程，只有真正的替换边界使用函数表。

## 阅读入口

| 文件 | 目的 |
| --- | --- |
| 本文 | 模块职责、依赖、迁移顺序和验收范围 |
| [PLAN.md](PLAN.md) | 分阶段计划、旧新 API 对应关系和完成状态 |
| [present_presenter.h](include/present_presenter.h) | GRAM／TE／FB Presenter 的语义迁移入口 |
| [present_lvgl.h](bindings/esp_lv_present_v2/include/present_lvgl.h) | 独立的可选 LVGL PARTIAL 绑定 |
| [FLOWS.md](FLOWS.md) | 三条显示路径、所有权、完成与失败时序 |
| [present_backend.h](include/present_backend.h) | 已实现 GRAM／TE／FB 的实验性后端接口及 panel adapter 契约 |
| [present_pixel_engine.h](include/present_pixel_engine.h) | 已实现 CPU 像素处理的任务接口及完成约定 |
| [present_lcd_spi.h](include/present_lcd_spi.h) | SPI/QSPI 的专用适配接口、独占要求和停止限制 |

## 模块边界

```text
应用 / LVGL / esp_lv_present
             |
pv2_presenter_*（旧 API 迁移见 PLAN）
             |
Presenter：校验外部句柄、调用序列、配置适配
             |
      单一 backend 对象
      /        |         \
    GRAM       TE         FB
    分块发送   合成与发送  扫描缓冲管理
      \        |         /
       pixel engine       panel adapter
       CPU / DMA2D / PPA   提交 / 硬件事件 / 停止保证
```

buffer 管理和完成记录属于各后端内部。几何、damage 运算、内存分配等无状态工具可共享，但不建立另一套全局流程控制器。

### Presenter

当前用独立的 `pv2_presenter_*` 符号提供 begin、acquire、submit、commit、cancel、drain、stop 及能力/状态查询。它拥有选定的 GRAM、TE 或 FB 后端和有限 receipt 记录，回收已完成记录；buffer 所有权仍由后端决定。当前 API 需显式移植，尚未实现旧 receipt generation、VSYNC tracking、等旧版完整协议，具体映射见 PLAN。

Presenter 不访问 tracker 的 pool、frame、switch 字段，不操作后端信号量，不执行 repair，不决定 buffer 何时释放。公共句柄校验与后端资源状态各有职责，不能维护两份可独立变化的 inflight 计数。

### 三个后端

| 后端 | 自己管理的完整流程 | 内部策略 |
| --- | --- | --- |
| GRAM | tile 获取、发送登记、发送、完成释放、失败收尾 | 单/双 tile、传输窗口与背压 |
| TE | tile 合成、图像补齐、拷贝完成、TE 准入、发送、退役 | TE 窗口、合成缓冲数量、局部更新 |
| FB | draw buffer 获取、内容补齐、提交切换、扫描确认、旧缓冲退役 | partition、full、direct、persistent |

每个后端提供一个包含生命周期及业务入口的完整描述。ISR 入口仍单独标注运行上下文，但与任务入口属于同一个对象和私有状态，不再由另一处配置独立配对。

FB 内部可以复用同一个 buffer pool；GRAM 与 FB 的 buffer 生命周期不同，不强求使用同一个泛化 pool 状态机。

### 像素引擎

输入是明确的源/目标 surface、区域和旋转。像素格式与字节序单独描述，stride 始终以字节为单位。

它负责选择 CPU、DMA2D 或 PPA，负责 M2M 加速器的 cache 访问权交接，并在任务完成或故障时通过 `on_event` 通知后端。worker 只是执行策略，不是一套对外拷贝 API。

不同缓冲区的源和目标严禁重叠。仅在无旋转、同布局且同起点的条件下允许显式原地字节序转换（`source.pixels == destination.pixels`）。GRAM 默认安排独立转换缓冲，也可通过 `in_place_convert` 启用原地转换（在驱动拒绝且未接受时自动翻转回调用方字节序）；若调用方通过 `submit_region_ex` 传入了带 padding 的 stride，则由像素引擎紧凑化到独立发送缓冲后再交给驱动。

没有旋转、stride padding 或字节序转换时允许后端直接发送已有 buffer，无需为了架构形式制造额外拷贝。

### 硬件适配

集中提供 panel 能力、传输提交、framebuffer 切换、事件归一化和安全停止。硬件限制留在这里，例如回调 CPU 约束、RGB/DSI 完成条件、DMA 对齐、PPA cache 要求。

驱动提交结果必须区分“未接受”与“已接受”。只有证明未接受，才允许撤销源 buffer 登记。遇到驱动可能部分接受后报错的情况，适配层必须暴露不确定状态并隔离资源，不能伪装成普通拒绝。

没有事务 ID 的驱动回调，由适配层依据已验证的 FIFO/串行契约关联。无法保证顺序的设备必须串行化，或判为不支持；不能凭最近一次提交 ID 猜测。

## 已实现的 SPI 适配边界

使用顺序：`pv2_lcd_spi_create` → `pv2_lcd_spi_get_interface` → 填入后端配置的 `panel` → 创建 GRAM。退出顺序：后端 `stop` 成功 → 释放 receipt 并销毁后端 → `pv2_lcd_spi_destroy` → 由调用方按需销毁 panel/IO。

adapter 借用已初始化的 panel/IO，不创建总线，不自动覆盖共享显示流程。attach 前 IO 必须空闲且没有现有 callback；使用期间独占颜色提交与回调注册。必须保证每次成功 draw 对应且仅对应一个按 FIFO 到达的颜色完成事件，驱动内部拆包只能在最后一块完成时通知。RGB/DSI/I80、共享 IO、非 FIFO 驱动不在本适配器支持范围。

局部画面从 buffer 起点读取，`logical_area.x/y` 是屏幕目的坐标；带 padding 时按 stride 逐行紧凑化，不从源图按 x/y 裁剪。原始绘制池地址只可用于注册，绘制前仍须持有对应 lease。可选 LVGL 绑定使用独立绘制缓冲，不把后端池直接注册给 LVGL；不能仅在 flush 里补一次 acquire 就宣称零拷贝所有权正确。

本地 ESP-IDF 的 `esp_lcd/spi/esp_lcd_panel_io_spi.c` 会逐块入队颜色数据。因此进入 `draw_bitmap` 后的任何错误都保守视为 UNCERTAIN：消费 buffer，关闭新提交，等待最终完成。若部分入队后永远没有最终回调，stop 会超时并保留对象与 buffer；当前没有通用的安全强制中止接口。禁止以清空软件队列或无界阻塞的 `tx_param` 伪造安全停止。

`stop` 只有在 FIFO 空且当前 callback 已退出时才解除回调注册。Host 用模拟驱动覆盖 FIFO/早到事件、cache 失败、布局拒绝、不确定接受、解除回调失败和回调正在运行时的停止。真实板上的总线、时序、驱动配置与 cache 行为留待后续验证。

## Presenter 与 LVGL 接入

直接绘制使用 `pv2_presenter_create → begin_next_frame → acquire_buffer → submit_buffer → commit_frame`。`submit_buffer` 接收 inclusive 屏幕坐标，源像素从 lease 起点读取；`resolve_rows` 按实际 stride 和容量限制行数。任务侧调用必须来自创建者。

`commit_frame` 不等 DMA 完成，Presenter 保存 backend receipt，后续 begin/status/drain 回收终态记录。`drain` 等待此前提交，成功后仍可继续 begin；`stop` 则进入关闭流程。异步故障保存在状态里并阻止后续 begin，但不妨碍排空和安全销毁。绘制失败后的尚未提交 lease 需要 cancel 收尾。

LVGL 应用先初始化 LVGL/tick 和 Presenter，再调用 `pv2_lvgl_create`。binding 不修改全局 tick，不创建 LVGL 线程。UI 操作与 `lv_timer_handler` 在 Presenter 创建任务上串行执行。display 的 user_data、flush callback、draw buffers 由 binding 保留，应用只操作 UI，不自行覆盖这些字段或直接删除 display。

FULL/DIRECT 使用独立的整屏 LVGL buffer；FULL 每次全屏刷新，DIRECT 从整屏源的脏区坐标复制到完整 lease 的同坐标。它们不是零拷贝：保留独立源能让 LVGL flush-ready 与 scanout 的长期借用解耦。PARTIAL flush 把独立 LVGL buffer 复制进已获取的 lease，必要时拆 band。源 stride 可含 padding；RGB888 从 LVGL 的 B/G/R 存储转成 PV2 的 R/G/B。flush-ready 只保证 LVGL 源已不再使用，不承诺屏幕发送结束；DMA 始终读取另一个后端 buffer。

失败暂停该 display 的刷新定时器，并尝试取消当前帧。`pv2_lvgl_get_error` 可读取错误；不自动重试已部分发送的图像。销毁顺序为 binding → Presenter stop/destroy → SPI adapter destroy。binding 取消超时保持对象有效，待传输完成后重试。与旧适配器相比，这一版使用额外绘制缓冲和一次 CPU 复制，零拷贝待后续单独优化。

ESP-IDF 应用通过 EXTRA_COMPONENT_DIRS 加入核心目录、`bindings/esp_lv_present_v2` 和已有 LVGL 目录；若限制 COMPONENTS，必须同时包含这些组件及实际 LVGL 组件名（常见为 `lvgl` 或 `lvgl__lvgl`）。smoke 的 `--with-lvgl` 已统一处理此配置。

## 状态归属与并发

| 对象 | 状态归属 | 其他模块可以做什么 |
| --- | --- | --- |
| 外部 frame/lease 句柄 | Presenter 的句柄校验层 | 提交句柄，不解引用后端内存 |
| frame 构建、提交、终结 | 后端 | 通过后端 API 操作 |
| tile、draw FB、scanout FB | 后端 buffer 状态 | 通过 lease 获取访问权 |
| 像素任务及其 src/dst 借用 | 像素引擎 | query/wait，完成后释放任务记录 |
| 硬件事务和回调关联 | 硬件适配层 | 提交及消费已关联事件 |
| 对外 receipt | 后端完成记录 | 查询/等待；显式释放记录 |

一实例只允许一个 producer 构建 frame；不同实例可以并行。共享 DMA/PPA 的排队由像素引擎处理。等待不持有 ISR 自旋锁。

ISR 只推进必要的完成/所有权状态并唤醒任务，禁止阻塞、分配内存、执行 repair 或调用任意用户回调。不得把 correctness 寄托在一个可能满而丢事件的通知队列；通知只表示“状态可能变化”，实际完成条件保存在受锁保护的记录中。

句柄包含实例与 generation 信息，拒绝旧 frame、旧 lease、重复提交、跨实例使用。回调携带的 cookie 在硬件彻底停止前保持有效，generation 校验不能替代对象生命周期保证。

## 计划的实现布局

以下是完整框架的目标布局。当前实际生产源为 `src/present_os.c`、`src/presenter.c`、`src/pixel/pixel_engine.c`、`src/backend/gram.c`、`src/platform/lcd_spi.c`，以及可选的 `bindings/esp_lv_present_v2/present_lvgl.c`；其余按后端推进，不创建占位成功函数：

```text
src/
  presenter.c                 公共 API 适配及句柄校验
  backend/
    backend.c                 选择、创建和统一分发
    gram.c                    GRAM 完整流程及私有状态
    te.c                      TE 完整流程及私有状态
    fb.c                      FB 完整流程及私有状态
  pixel/
    pixel_engine.c            任务生命周期、调度和完成
    pixel_cpu.c               软件处理及参考实现
    pixel_accel.c             DMA2D/PPA 能力选择与驱动封装
  platform/
    lcd_spi.c                 已实现 SPI/QSPI panel 适配
    panel_adapter.c           后续其他 panel 的适配边界
    cache.c                   对齐和 cache 原语
  common/
    geometry.c                坐标、裁剪、damage 运算
```

后端变大时，允许在后端子目录拆 transport、pool 等私有文件；拆分依据是职责与独立验证需求。仅被单处调用且没有独立职责的转发函数留在调用方，不建立新模块。

## 从旧实现迁移

| 现有实现 | 新归属 | 迁移要求 |
| --- | --- | --- |
| mode ops 与 endpoint ops | 同一个 backend 描述 | 不再分别绑定流程与生命周期 |
| gram transport 与 gram 资源 | GRAM 后端 | 保留早到回调、FIFO、背压约束 |
| te render/repair/pool/transport | TE 后端 | 保留 TE 准入、停止、补齐行为 |
| fb render/pipeline/fence/repair | FB 后端 | 保留扫描确认与旧 buffer 退役条件 |
| async copy/blit/rotate/部分 PPA 封装 | 像素引擎 | 统一 stride、字节序、完成与超时语义 |
| target/profile/resolve | 创建期配置与硬件适配 | 创建时拒绝非法组合，运行时不反复推导 |
| frame tracker | 后端私有状态及有限公共记录工具 | 不照搬成可被所有模块直接修改的大结构 |

顺序：先把旧实现作为行为基线，补齐关键异常测试；再独立实现像素引擎和 GRAM；之后 TE；最后迁移 FB 各模式与现有适配层。每个后端成熟后再通过构建选择接入，不将一帧拆成新旧状态管理混用。

不整体复制旧代码再改名。几何算法、已验证的硬件适配细节和有意义的测试可以迁移；状态流程按新边界重写。

## 验收与进入实现的条件

必须覆盖：驱动同步完成回调、迟到回调、提交前拒绝、已接受后故障、DMA 等待超时、停止超时、带 padding 的 stride、旋转与字节序组合、资源耗尽和跨实例并行。

三个关键判据：修改 GRAM 回滚主要只涉及 GRAM；增加像素加速实现不修改后端；任何 buffer 的释放都能找到唯一的所有权判据。

性能对照使用相同分辨率、色深、缓冲数量与 PSRAM 配置，记录 FPS、CPU 占用、峰值 RAM、内存带宽、提交至完成延迟，以及整屏/局部更新的像素正确性。目录和函数更少不构成性能或可靠性证明。

后续接入前需冻结：现有公共 API 的逐项兼容表、各真实 panel 的接受/完成/停止保证，以及单 FB persistent 的撕裂与写入能力契约。SPI/QSPI 可使用新增 adapter，其他驱动仍需各自适配。Host fake 与 IDF 交叉编译不证明真实 LCD/DMA 时序。

## 异步像素与 TE 接入（2026-10-30）

```c
pv2_presenter_config_t cfg = {0};
/* 先填写尺寸、tile_rows、buffer_count、max_frames、panel 和 TE 时间预算。 */
cfg.backend_kind = PV2_PRESENTER_TE;
cfg.backend.async_copy = true;       /* 独立 FIFO copy worker，默认 CPU 算法 */
cfg.background_service = true;      /* 独立 sender；false 时由 owner 调 service */
cfg.timeout_ms = 1000;
```

`copy_execute` 可替换 worker 内的执行函数；返回时必须保证读写全部结束，失败也一样。默认 worker 执行 CPU copy；可配置 `pv2_pixel_accel_execute`。GRAM／TE／FB 全契约接受 async_copy，FB FULL 无旋转时没有额外 copy，DIRECT 的 prefill/repair 与旋转转换可异步执行。

TE submit 接受后消费 lease，但 tile 保留到 copy 终态；end 可在 copy 未完成时返回，service 在 COPY_DRAIN 后验证实际覆盖、补图并等待新 TE 边沿。异步首帧覆盖不足或执行失败以 receipt/fault 报告，不会发送未完成画面。copy 通知只唤醒 owner/sender，不在回调里 repair 或发送。异步 repair 每次提交一个连续段并返回 pending；service 在 ticket 终态后推进下一段，不在 gate 内等待整段硬件完成。

像素 cancel 对 QUEUED 请求标记取消；RUNNING 请求安全排空。超时保留借用。stop 排空全部请求并 join copy worker，包含用户通知回调；stop/destroy 由一个生命周期 owner 串行调用。每个 ticket 最多一个阻塞等待者。TE stop 在同一预算内先排空 copy，再 quiesce panel。

`pv2_presenter_get_required_frame_buffer_count` 是无分配的模式查询；`get_draw_buffers` 只查看池地址，不授予写权限。`rebind_producer` 由新任务调用，前提是旧任务已退出所有 task API；不允许带未封闭帧或 lease 交接，已封帧传输继续完成。LVGL 的任务归属和锁不随之自动迁移。

Host 新增队列满、描述符快照、重叠 FIFO、排队取消、延迟读取、copy 故障、stop 超时重试、回调退出、TE 旧边沿失效及真实 LVGL 异步后台发送检查。四组相关测试各重复 100 次通过；这不是竞态穷举或真机时序验证。

### FB PARTITION 异步路径

设置 `backend.async_copy=true` 可将 scratch → candidate 的拷贝交给 pixel worker。scratch 当前仍为一个：完成前 acquire 返回 TIMEOUT，由 Presenter 按总预算等待通知并重试。end 遇到未完成 copy 时进入 COPY_DRAIN，后续 service 验证实际覆盖再将 candidate 置 READY；期间新帧准入返回可重试的 NO_MEM。尚未完成的 candidate 不成为基线，也不会被扫描。

取消构建进入 CANCELING，待 copy 排空后自动回收。stop 先 join pixel，再用剩余预算 quiesce 扫描面板；超时保留 scratch/candidate/receipt。async_copy 配置下，基线预填和 DIRECT 恢复也走 ticket；prefill 完成前不借出 lease。

新增 Host 用例覆盖延迟读取、禁止提前扫描、取消重试、stop 超时、执行失败、不完整首帧及无 receipt 回收；真实 LVGL 覆盖异步双/三缓冲 PARTIAL、RGB565/RGB888、多脏区和扫描源保留。当时普通与 ASan/UBSan 14/14 通过，泄漏检测关闭；未上机。

### GRAM 异步提交

`backend.async_copy=true` 使用独立转换缓冲，把打包/字节序转换交给 pixel worker；当前即使布局无需转换也执行独立拷贝，以保持同一借用协议。此选项与 `in_place_convert` 互斥。内存增加每个 tile 一份转换缓冲；默认同步行为保持不变。

submit 返回 ESP_OK 即消费 lease，pending 同时涵盖 COPY 和 panel 读取。`pv2_gram_service` 按提交 cookie 顺序处理 `COPYING → READY → SUBMITTING → INFLIGHT`；未完成的早期 copy 和被拒收的早期发送都会挡住后续发送。copy 回调只通知任务，不发送硬件。

同步 submit 的明确拒收仍返回原 lease；异步 copy 已接受后的明确拒收由 service 返回可重试错误，后端保留 READY 及像素，调用者再次 service 即可，不重新提交旧 lease。接受状态不确定或接受后错误保存 sticky fault，缓冲留到 panel 完成或安全 quiesce。query/wait/acquire 也会泵送；无硬件进展时仍遵循原始超时预算。

取消仅排空已接受 copy，未发送块不再发送。stop 在剩余预算内先 join pixel worker，再 quiesce panel；任何阶段超时保留对象。Presenter 可同时配置 `backend.async_copy=true`、`background_service=true`，不需要新增 LVGL 分支。

新增 Host 检查：延迟 copy、copy 错误、拒收重试、uncertain、取消、stop 重试、padding+swap、两块 FIFO 拒收阻塞以及真实 LVGL 后台发送/源复用。当时普通与 ASan/UBSan 14/14；GRAM 和 LVGL 集成组各重复 100 次通过。泄漏检测关闭；硬件加速器和实际屏幕仍未验收。


## 本轮新增 API 与使用约束

### 旋转与显式追踪

```c
pv2_presenter_receipt_t receipt;
ESP_ERROR_CHECK(pv2_presenter_commit_with_receipt(p, NULL, &receipt));
// 在 owner 任务上继续 service，或启用 background_service；事件源独立运行。
pv2_frame_status_t status;
esp_err_t err = pv2_presenter_wait_frame(p, receipt, PV2_COMPLETION_PRESENT_CONFIRMED, 100, &status);
// TIMEOUT 只结束等待；成功还要检查 status.result。GRAM/TE 不支持 PRESENT_CONFIRMED。
if (err == ESP_OK) {
    ESP_ERROR_CHECK(pv2_presenter_release_receipt(p, receipt));
}
```

`cancel_tracking` 可以提前放弃观察，不撤回帧或释放扫描源。显式观察记录最多 32 个，满时 commit 返回 NO_MEM 并保留打开的 frame；停止后仍须释放观察记录才能 destroy。

旋转前销毁／解绑 renderer，再 `pv2_presenter_set_rotation(p, rotation, timeout_ms)`；此 API 排空已封帧，超时保留旧方向。重新读取 caps、重建 LVGL binding、全屏重绘。`pv2_presenter_map_point` 把物理触摸坐标逆变换为当前逻辑坐标。旋转不写 panel MADCTL；FB 保留物理扫描池，新建同数量逻辑 buffer，非零旋转经过逻辑→扫描转换。排空后切回 0° 会释放额外逻辑池，恢复直接绘制；正在扫描的物理 FB 仍保持不可借出，重新建立基线必须全屏重绘。

### DSI／RGB bounce 接入

应用负责创建、初始化 panel 和总线，使用驱动的 2～3 张紧凑 framebuffer。独占回调注册；初始 FB0 正在扫描。`pv2_lcd_scanout_create/get_buffers/get_interface` 提供池和 panel 接口，设置 backend `external_buffers`、`external_buffer_bytes`、`initial_scanout_buffer=buffers[0]`，否则首次绘制可能覆盖正在扫描的 FB0。

DSI 与 RGB bounce 连续刷新共用 `lcd_scanout.c`；配置须与实际 panel 一致，禁止额外应用切换、driver rotation/mirror、bounce callback。非 bounce RGB 返回 NOT_SUPPORTED。以 draw_bitmap 成功返回后的 **两次 frame_buf_complete** 为保守确认，隔离一次已读到旧索引的 ISR；不使用 color_done 或裸 VSYNC 冒充退役。此规则基于本地 IDF 6.2 驱动，升级需复核，尚未证明真实屏幕无撕裂。

默认 panel 借用；提供 `stop_scanout(user, timeout_ms)`，成功必须证明 DMA 停止且回调退出。也可设置 `delete_panel_on_stop=true` 在 attach 成功时转移 panel 所有权，通过驱动删除停止；此删除不承诺硬 deadline。仅关闭背光或发送 display-off 不合格。按 presenter stop/destroy → adapter destroy → 剩余 panel/bus 清理，失败保留实例重试。若驱动挂有额外 DMA2D hook，板级 stop 应先安全移除。

### DMA2D／PPA executor

```c
pv2_pixel_accel_t *accel;
pv2_pixel_accel_config_t ac = {.kind=PV2_ACCEL_PPA, .cpu_fallback=true};
ESP_ERROR_CHECK(pv2_pixel_accel_create(&ac, &accel));
config.backend.async_copy = true;
config.backend.copy_execute = pv2_pixel_accel_execute;
config.backend.copy_user = accel;
// 所有使用 accel 的 presenter/pixel engine 成功 stop/join 后，才 destroy accel。
```

DMA2D 仅同格式 0°／不交换字节，PPA 支持四方向和 RGB565 byte swap；不做格式转换。提交前校验实际 cache line、完整行容量、对齐与 14-bit 描述符范围；PPA SRM 不支持 MSPI strict alignment 外部内存，会在提交前返回 NOT_SUPPORTED 或退回 CPU。DMA2D 的窗口／行 stride 也必须满足该约束。fallback 只发生在提交之前，硬件错误后绝不重复 CPU 写入。

目标实际行带（按旋转后的高度，包含行内 padding）在独占 allocation 内对齐，先 C2M 保留边缘脏行，再硬件访问，完成后对同一行带 aligned M2C；不使用 M2C+UNALIGNED，不向 allocation 外扩。异步执行器持续占有源和目标至真实完成；硬件不回调时，外层 stop 可以超时，但不释放资源。内部 buffer 按实际 capacity 判定，非对齐 capacity 会退回 CPU。DMA2D 使用本地 IDF 私有接口，不能当稳定跨版本公共 API。

### 可选 LVGL 零拷贝

`pv2_lvgl_create_zero_copy(p, &binding)` 仅接受多 FB 的 FULL／RGB565/native，且 stride 与 LVGL 一致。RENDER_START 时 begin+acquire，LVGL 才获得 backend lease；最后 flush submit+commit，解绑至私有安全 buffer 后 flush_ready。无脏区时不申请 lease；REFR_READY 归还异常中断时未提交的 lease。准入失败暂停刷新，私有 buffer 接住 LVGL 本轮后续处理，不能让它继续写入扫描源。

该模式省去 LVGL→lease 的复制，仍保留一张私有安全 buffer，不宣称节省这张内存。旋转时后端仍需转换，不能称端到端无复制。PARTIAL／DIRECT／RGB888／不兼容 stride 使用原 `pv2_lvgl_create`；不会静默切换或降低颜色正确性。binding 销毁后，已封帧仍由 backend 完成。


## Review 收口（2026-10-30）

- 像素格式统一改名为 `PV2_FORMAT_RGB565`／`PV2_FORMAT_RGB888`，实验 API 调用方须迁移；没有保留双命名别名。数值不变。
- FB 状态增加 `FB_SLOT_` 前缀；共享资源上限放在私有 `present_limits.h`。`PV2_BUFFER_ALIGNMENT` 是分配策略，不能代替实际 cache line 查询。
- `pv2_progress_fn`、`pv2_stop_scanout_fn`、私有 worker entry typedef 统一回调类型；ops 按声明顺序初始化。公开头文件补齐形参及 Doxygen。
- coverage 行内按头尾掩码及 `memset` 标记，全覆盖检查逐字节并处理尾部有效位。repair 连续段搜索仍保留原语义，没有引入第二套状态机。
- cache Host 测试检查同步地址／长度、padding、90°/270° 高度、最后一行和非目标像素；这些是范围及像素验证，不等于真实 PSRAM 带宽测量。
- rotation 的 Host 链接包装只统计像素池，验证重复旋转、回零释放、内部／外部所有权和最终归零；不冒充全进程泄漏检测。

格式命令仍使用统一入口：`python3 tools/dev.py format`，只检查用 `python3 tools/dev.py format --check`。读取仓库 `tools/ci/astyle-rules.yml`，增加控制流大括号及单行展开；不修改仓库全局规则。本机 AStyle 3.1，仓库 CI 固定 3.4.7；本地幂等检查通过不表示该版本 CI 已运行。

## 后端公共规则与自治边界

三个后端复用私有 [present_records.h](src/present_records.h)：公共记录字段、token
查找、状态输出、首次故障、取消幂等、几何校验和超时预算。GRAM 通过组合扩展
记录；TE/FB 复用同一记录类型。锁、缓冲所有权、完成时机和回收仍由各后端控制。
公共模块不持有后端指针、不推进状态机。新增跨后端 records 契约测试后，当前
Host/ASan+UBSan（泄漏检测关闭）均为 21 组通过；本次重构仅完成本地验证。
