> 2026-11-03 默认入口已切到 V2，旧实现已移除；当前布局、支持范围及验证见 [DEFAULT_SWITCH.md](DEFAULT_SWITCH.md)。下文旧阶段中的“尚未切换”与 V1 源码路径只用于历史追溯。

## 2026-11-03：兼容阻塞与交付边界修复

本轮完成前次检查点的三项缺陷与交付边界修复：

| 项目 | 实现及回归 |
| --- | --- |
| TE gate 阻塞 | 独立非阻塞窗口查询，移除旧 semaphore wait；双边沿与 phase timer 驱动。真实 gate 实现使用虚拟时钟测试无边沿、窗口前/边界/过期、after-edge、超窗与 timer 销毁重试 |
| stop 未封帧 | GRAM/TE/FB 无 lease 构建先取消再停止；DIRECT prefill 阻塞时保留对象，重试成功且无 sticky fault |
| TE_CANCELING | copy 排空后正常发布取消，再执行 panel quiesce；测试 cancel(0)→stop(0)→放行 copy→stop 成功，无假故障 |
| 自动双 tile | GRAM、异步 GRAM、TE、FB 实际像素分配故障注入；自动降为一块且 plan 与池一致，显式两块保留 NO_MEM |
| 内存预估 | TE 内部 compose 与 GRAM 转换槽计入预留；PSRAM 一块可用时保留外部内存偏好 |
| 独立交付 | 兼容组件拥有 TE/geometry 实现、四份公开头文件与 esp_lcd 公开依赖；不引用 V1 私有目录 |

普通 Host＋真实 LVGL 35/35、ASan/UBSan/LSan 35/35（`ASAN_OPTIONS=detect_leaks=1`）通过。P4／S3／S31 独立兼容组件离线编译、链接通过；四组相关回归各连续 50 次（共 200 次）通过。`git diff --check` 与组件格式检查通过。

本轮不上板、不切换默认组件。GPIO IPC join 的原生调用仍没有硬超时保证；框架在前后检查预算并保留超时对象。Host 不证明真实 TE 光学窗口或竞态穷举。

# `esp_display_present_v2` 设计与代码风格 Review 报告

## 2026-11-02 检查点：当时待修复项（本轮已处理）

本次提交保存现有实现与验收结果，不表示以下问题已解决，默认依赖仍不切换。

- P1：兼容 TE gate 在 Presenter gate 内调用阻塞式 wait_for_vsync，没有传递调用方剩余预算；Host 替身立即返回，未覆盖真实阻塞。应改为当前窗口检查与事件再次推进。
- P2：兼容 stop 对任意 active frame 一律拒绝，包含没有持有 lease 的未封帧状态；需对齐 V1 的实际租借与访问排空条件。
- P2：自动双 tile 缺少 V1 optional_second 分配失败退回单 tile 的行为；需覆盖碎片化及初始化期间内存不足，显式双 tile 不得自动降级。
- 交付边界：兼容组件通过相对路径编译 V1 TE 私有源文件；发布前应明确共享叶模块的归属与依赖。

本次 review 复跑现有 Host 构建 34/34 通过；未修改上述生产逻辑，未重新上板。

本轮代码、性能和未通过项统一见 [收敛验收记录](test_apps/legacy_compat/RESULTS_CLOSE_20261102.md)。

> 2026-11-02 当前收敛状态见 [V1 兼容状态](V1_COMPATIBILITY_GAPS.md) 与 [收敛计划](CONVERGENCE_PLAN.md)。下面旧日期的数量和限制属于历史切片，不能替代当前门面验收。

> 2026-10-30：已继续执行整体迁移。下面“历史 review”保留当时的判断；当前状态以本节和 PLAN.md 为准。

## 当前整体修改的 Review 入口

- 公共状态：`present_frame_core` 是三后端帧/receipt/fault/cancel 的共同核心；`present_transaction` 归并提交返回与早到完成。
- 内存所有权：`present_allocation` 区分 B/C/T/S 和独占 writer，FB receipt 完成仍保留扫描引用；GRAM 源/转换缓冲作为一个复用单元保守管理。
- 数据面：`present_image` 统一 coverage、版本、history/repair。TE composer 与 FB 仍承担不同的 copy/输出阶段，未新增通用 stage 分发表。
- 拓扑：FB PARTIAL 旋转零逻辑全屏，FULL/DIRECT 一张共享逻辑画布；FB 多 tile 与图像数解耦；TE 显式单/双 compose 可重叠构建和发送。
- 准入：规划器纯查询能力；运行时必须地址 probe。动态 TE 需要明确线性模型；不把实际传输通过等同光学无撕裂。
- 集成：LVGL 渲染前区域协商、运行时旋转更新绑定；copy 完成才能复用源，stop 超时保留资源。
- 回归：29/29 Host/LVGL 与 29/29 ASan/UBSan，泄漏检测关闭。P4/S3 五模式和 S31 全屏输出证据见 RESULTS。

剩余明确限制：多逻辑并发画布、特殊池 stride/base alignment、FULL/DIRECT 字节序变化仍拒绝；扫描模型光学验证、长期压力、完整峰值内存测量仍不能标完成。具体见 [PLAN.md](PLAN.md)。

## 历史 review（下文按实施时间保留）


## 当前统一复核（2026-10-30，八项代码收敛）

本轮实现、限制及验收清单以 PLAN.md 为准。本轮普通 Host 20/20、ASan/UBSan 20/20（detect_leaks=0）与 P4 + LVGL、ESP32-S3 核心 smoke 离线构建通过；六组新增／相关测试各重复 100 次通过。上述代码 review 阶段没有执行硬件测试。后续已完成 S3 RGB／P4 MIPI CPU DOUBLE_FULL 首轮上板，两板用户确认画面正常，见 [板测记录](test_apps/boards/RESULTS.md)。其他模式及长期时序尚待验收；不能外推为全模式无撕裂、硬件吞吐或竞态穷举。

| 复核项 | 处理结果 | 保留限制 |
| --- | --- | --- |
| TE/FB repair 阻塞 | 异步模式按 ticket 分段推进，FB prefill 未完成不发 lease；新增条件变量 gated executor 测试 | 同步模式仍同步；裸后端等待部分轮询 |
| DIRECT 重复写入 | prefill 与 end repair 均可异步，保留其读旧图与防御未声明写入的契约 | 尚未消除两次写放大；不能把异步等同于减少带宽 |
| receipt 与 ISR | TE/FB receipt 回收补锁；FB begin/cancel 元数据切换补锁；Presenter 新增独立观察记录，终态快照与扫描所有权分离 | 单 producer 仍必需；外部事件源 destroy 前须退出 |
| cache | 查询实际地址 cache line，目标独占 allocation C2M→DMA→aligned M2C，MSPI 限制在提交前检查 | 未实测 PSRAM；M2C 不使用 UNALIGNED；加速器可能回退 CPU |
| scanout 确认 | DSI／RGB bounce 生产桥接；初始 FB0 保留；两次 post-submit frame_buf_complete 屏障，错误视为 uncertain | 非 bounce RGB 不支持；规则绑定审计的本地 IDF 6.2；panel 删除无硬 deadline |
| rotation | 初始及运行时四方向，物理扫描池稳定；触摸逆变换和超时保留测试 | 调用方重建 renderer、全屏重绘；FB 增加逻辑池及一次转换 |
| 加速器退出 | DMA2D 等待真实完成，PPA blocking；stop/join 后才能销毁共享 executor，注销失败保留对象 | 无强制 DMA abort；DMA2D 私有 API 需版本复核 |
| LVGL 直绘 | RENDER_START 获取 lease，flush_ready 前解绑；无脏区不申请 lease；超时在私有安全缓冲上完成本轮并暂停 | 仅多 FB FULL/RGB565/native、兼容 stride；仍保留私有安全缓冲 |

新增测试：repair、rotation、scanout、accel、lvgl_zero；旧 GRAM／TE／FB、Presenter、scheduler、真实 LVGL 复制模式全部保留。执行器 stub 使用独立描述符解释器对照像素；延迟 DMA 完成时 pixel stop 超时保留。原先 smoke 的异步 FB acquire 改为有界重试，避免新增异步 prefill 后误判失败。

本轮没有将 V1 VSYNC generation 改名后冒充完整迁移。Presenter 的 SOURCE_RELEASED／PRESENT_CONFIRMED 以实际后端能力为准；cancel_tracking 仅释放观察权。后续 review 优先关注驱动版本假设、stop 钩子的退出保证和旋转带来的额外内存，性能结论等上板数据。

## 用户收官建议处理（2026-10-30）

| 建议 | 落实与验证 |
| --- | --- |
| 整张 FB cache 同步放大 | 改为实际目标行带，90°/270° 使用变换后的高度；首尾对齐且不得越过 allocation，覆盖 PPA 扩展行 invalidate；测试检查准确地址和长度 |
| 旋转回零恢复内存／带宽 | 释放逻辑 draw 池，恢复物理 surface 并转移所有权标志；保留 SCANNING 槽位，清除基线；两轮四方向与像素池记账验证，无零度额外转换 |
| coverage 逐像素循环 | 头尾位掩码、中间字节 memset，全覆盖按字节并掩盖尾部无效位；新增穷举短区间、哨兵及逐个缺失位测试 |
| DSI 两次事件可能不持续 | 核实本地驱动重启 DMA 后持续回调；保留两次确认，上板增加单次 submit 后周期事件观测，不未经证明改为一次 |
| 命名和结构 | FORMAT 枚举、FB_SLOT 前缀、回调 typedef、私有共享上限、ops 顺序；保留后端各自生命周期，不为对称性强制合并 |
| 排版与头文件 | 标准 SPDX、控制流大括号、多行语句、分段校验、移除模式映射嵌套三元、公开形参及 Doxygen；使用仓库 AStyle 规则 |

本机 AStyle 3.1，与 CI 的 3.4.7 不同，未声称通过 CI 固定版本。Doxygen 本地生成 XML，文档解析零警告；新增／相关 coverage、rotation、accel、repair 四组各重复 100 次通过。三个性能修改仍需上板测收益；Host cache 范围数值不是实际 PSRAM 流量，像素池记账不是全进程泄漏检测。原始建议中的“2 MB×20 次”只能描述同步地址范围，不能直接当作实际总线写入量。

## 原始 Review 与历史记录

以下保留原始建议用于追溯；其中旧测试数量、行号和“尚未实现”描述是当时状态，已完成项以上表及 PLAN.md 为准。原报告中的评价不是新增验收证据。

---

## 一、 总体评估结论

**总体方向：架构收敛极其扎实，模块边界清晰，所有权与并发不变式闭环严密。**

相比 V1（`esp_display_present`）将流程拆分为 `mode ops`、`endpoint ops`、`pipeline stages` 以及多模块共享修改的 `frame_tracker`，V2 采用 **“内聚的三大后端（GRAM / TE / FB）+ 独立像素引擎（Pixel Engine）+ 硬件适配边界（Panel Adapter）+ 单门面串行化（Presenter Gate）”**。每一次提交、完成、回滚、基线维护与故障隔离都严格收敛在各自后端内部；上层 Presenter 与 LVGL 绑定只管调用序列、有界回执回收与独立源缓冲复制，彻底消除了跨模块共享状态机带来的组合爆炸。

---

## 二、 核心架构与状态机设计亮点

### 1. 所有权与时间线语义彻底解耦
- 严格区分了 **“函数返回”**、**“LVGL 源缓冲复用 (`flush_ready`)”**、**“源缓冲释放 (`PV2_COMPLETION_SOURCE_RELEASED`)”**、**“显示确认 (`PV2_COMPLETION_PRESENT_CONFIRMED`)”** 与 **“旧扫描缓冲退役 (`SCANOUT_CONFIRMED` -> `FREE`)”**。
- **Wait 超时契约清晰**：超时仅结束本次等待（返回 `ESP_ERR_TIMEOUT`），绝不自动取消硬件访问或提前释放 buffer；Receipt 仅代表完成记录的查询权，绝不绑定当前 scanout buffer 的物理所有权。

### 2. 显式建模驱动层的“不确定接受 (`PV2_TX_UNCERTAIN`)”与早到回调
- 在 `lcd_spi.c` 中，针对 ESP-IDF `esp_lcd_panel_io_spi.c` 内部拆包入队的真实行为，一旦 `esp_lcd_panel_draw_bitmap` 返回错误，保守判定为 `PV2_TX_UNCERTAIN`：消费 lease、隔离缓冲、停止新提交并等待最终完成，杜绝了 DMA 仍在读内存时将 buffer 退回池中的 UAF / 花屏隐患。
- 在 `gram.c`、`te.c`、`fb.c` 中均通过 `SUBMITTING` 阶段预登记 `cookie` + 早到完成标记（`early_done` / `done` / `confirmed`），完美处理了“驱动函数返回前同步或抢占触发 ISR 完成回调”的竞态。

### 3. Fire-and-Forget 自动回收、Sticky Fault (`get_fault`) 与瞬态拒收分离
- `end(backend, frame, NULL)` 支持无外部 Receipt 的自动槽位回收，避免常规刷屏耗尽 `max_frames`。
- 同时引入 `pv2_backend_fault_t` sticky fault（记录首个已接受操作或通道级失败的 `result`、`frame` token 和 `transaction_cookie`），保证即使无 Receipt 帧记录被自动回收，上层依然能通过 `get_fault` 准确获知异步硬件故障，且三个后端的 sticky fault 均阻止新帧准入。
- 在 Presenter 层明确区分 `first_error`（不可恢复的 sticky fault）与 `last_service_error`（例如 TE/FB 驱动明确拒收 `PV2_TX_REJECTED`，保持 `READY` 等待下一周期边沿或下一次 pump 重试），不将可重试的瞬态拒收误判为致命故障。

### 4. TE CPU 后端的精确覆盖跟踪与单调边沿准入 (`te_composer.c` / `te.c`)
- **真实写入位图校验**：不采信调用方未验证的 `full_redraw` 或 damage 提示，使用每像素 1 bit 的 `coverage` 位图记录真实成功写入；首帧必须 100% 覆盖全屏才能封帧（否则保留 `BUILDING` 允许继续补写），后续帧仅从上一个成功发送的稳定基线（`baseline`）按行内连续未覆盖片段（run-length spans）执行 repair。
- **准入安全预算与旧边沿丢弃**：强制要求配置 `te_window_us` 与 `te_transfer_budget_us`（`budget < window`），任务侧 `offer` 仅在 `edge >= ready`、`now >= edge` 且 `now - edge < (window - budget)` 时放行；`seal` 完成时主动清除 `repair` 期间到达的旧边沿，防止用过期边沿积攒发送额度。

### 5. FB 后端的多缓冲流水与“最新封帧基线” (`fb.c`)
- **扫描退役强证据**：仅当收到匹配 `cookie` 的成功 `PV2_EVENT_SCANOUT_CONFIRMED` 时，才同时认定新 FB 已成为扫描源且旧 FB 已停止读取并退回 `FREE`；忽略普通 `TRANSFER_DONE`。
- **三缓冲增量基线一致**：在双/三缓冲的 `PARTITION` 与 `DIRECT` 模式下，新帧从**最新封闭且不可变的候选帧（`baseline = active` at `end`）**预填或恢复未声明区域，而非从落后的当前扫描帧复制，保证三缓冲排队时中间帧的增量绘制不丢失。
- **单 FB 撕裂契约显式化**：`buffer_count == 1`（`PV2_MODE_NONE`）必须显式开启 `allow_active_scanout_writes`，且在 `cancel_frame` 后强制清空 `baseline`（要求下一帧全屏重绘），绝不伪装成无撕裂的双缓冲。

### 6. Presenter 事件等待、双信号域隔离与可选后台线程 (`presenter.c` / `present_event.c`)
- **双 Latched Event 防信号互吞**：拆分 `owner` 等待的 `changed` 事件与 `sender` 线程等待的 `work` 事件；`sender` 执行完一次后端 `service` 后只唤醒 `changed`（不自唤醒空转），从根源上杜绝了 worker 消费掉 owner 唤醒信号或死循环忙等。
- **锁外等待与统一超时预算**：`owner` 在 `begin_next_frame`、`acquire_buffer`、`cancel_frame`、`drain` 等待期间释放 `gate` 互斥锁，被唤醒后重新拿锁并重查权威状态；整个等待循环共享调用入口处的单一 `timeout_ms` 剩余预算，不逐次重置。
- **两阶段安全停止**：`pv2_presenter_stop` 先设置 `CLOSING` 并请求/等待 `worker` 线程退出（`pv2_worker_join`），确保没有并发的 `panel.submit` 在跑之后，再获取 `gate` 调用 `backend->stop` / `panel.quiesce`，严防并发调用 `submit` 与 `quiesce`。

### 7. LVGL 9.5+ 绑定的所有权解耦 (`present_lvgl.c`)
- 采用独立的 LVGL 绘制缓冲（PARTIAL 按 `draw_rows` 分配，FULL/DIRECT 分配整屏源缓冲），在 `flush` 中获取后端 lease、完成像素复制（含 PARTIAL 自动拆 band 与 RGB888 的 `B,G,R -> R,G,B` 归一化）并提交后，即可安全调用 `lv_display_flush_ready`，使 LVGL 的缓冲区复用与后端长期的 DMA / Scanout 生命周期彻底解耦。

---

## 三、 当前实现中发现的潜在细节问题与改进点

结合最新代码实现（`te.c`、`fb.c`、`presenter.c`、`present_lvgl.c`），以下几个具体实现细节值得在后续迭代中关注或优化：

### 1. `fb.c` 与 `te_composer.c` 中逐像素位图操作 (`cover` / `is_covered`) 的 CPU 性能开销
在 `src/backend/fb.c:337-349, 424-436` 以及 `src/backend/te_composer.c:110-115, 126-140` 中：
```337:344:components/display/tools/esp_display_present_v2/src/backend/fb.c
static void cover(pv2_backend_t *b, pv2_rect_t area)
{
    for (uint32_t y = area.y; y < area.y + area.height; y++) {
        for (uint32_t x = area.x; x < area.x + area.width; x++) {
            size_t i = (size_t)y * b->config.width + x;
            b->coverage[i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
}
```
以及 `fb_end_ex` 中 DIRECT 模式的逐像素恢复：
```434:434:components/display/tools/esp_display_present_v2/src/backend/fb.c
        for (size_t i = 0; i < pixels; i++) { if (!is_covered(b, i)) { memcpy(dst + i * bpp, src + i * bpp, bpp); } }
```
- **问题评估**：在小分辨率 Host 测试（如 4×4、8×6）下耗时可忽略，但在真实 RGB/DSI/TE 屏幕分辨率（如 `480×480` = 23 万像素、`800×480` = 38.4 万像素、`1024×600` = 61.4 万像素）下：
  - 每次全屏刷新执行 `cover()` + `fb_end_ex()` 逐 bit 检查 + DIRECT 逐像素 `memcpy(..., bpp)`，会在每个像素上做乘除移位分支，单帧仅位图扫描就会消耗数百万至上千万个 CPU 周期（在 240MHz ESP32-S3 上可达 **15~40 ms/帧**）。
- **改进建议**：
  1. 位图置位（`cover`）对行内中间完整的字节使用 `memset` 或按 `uint32_t`/`uint8_t` 掩码批量置 `0xFF`，仅首尾不足 8 像素处按 bit 置位。
  2. 全屏覆盖检查（`require_full`）按字节检查 `coverage[byte_idx] == 0xFF`（仅最后一个余数字节掩码检查），速度可提升 8~32 倍。
  3. `fb.c` 的 DIRECT 未覆盖区域恢复，应像 `te_composer.c` 那样合并为行内连续未覆盖区间（`memcpy(dst + start * bpp, src + start * bpp, run_len * bpp)`），避免逐像素调用 2/3 字节的 `memcpy`。

### 2. `present_lvgl.c` 在 `DIRECT` 模式下每次 Flush 重复拷贝整屏的写放大问题
查看 `bindings/esp_lv_present_v2/present_lvgl.c:56-95` 与 `src/backend/fb.c:350-390`：
- 在 LVGL 9 的 `LV_DISPLAY_RENDER_MODE_DIRECT` 模式下，如果一帧有 $K$ 个脏矩形（例如 5 个小图标更新），LVGL 在同一次 refresh 周期内会调用 $K$ 次 `flush_cb`，前 $K-1$ 次 `lv_display_flush_is_last()` 为 `false`，最后一次为 `true`。
- 当前 `copy_flush` 在每次 `flush` 被调用时，都会调 `pv2_presenter_acquire_buffer` -> 拷贝该脏区 -> `pv2_presenter_submit_buffer`（消费 lease）。
- 而在 `fb_end_ex`（`fb.c:430-436`）中，为了防止未声明区域被污染，会在 `end` 时把所有 `!is_covered` 的像素再从 `baseline` 扫一遍拷回来（其实在 `fb_begin` 第 293 行已经整帧 `memcpy` 过一次 `baseline` 了！）。
- **改进建议**：
  - 在 `fb_begin` 已经将 `baseline` 整帧预填到 `active` 的前提下，只要 `pv2_lvgl.c`（以及遵循规范的调用方）只往提交的 `area` 内写数据，其实无需在 `fb_end_ex` 再扫一遍全屏去恢复未覆盖像素；若为了防御直接操作裸指针的越界写而保留恢复逻辑，则 `fb_begin` 时的整帧 `memcpy` 对 `DIRECT` 模式就是重复拷贝（只需在 `fb_end_ex` 补齐 `!is_covered` 即可，省去 `fb_begin` 的整屏拷贝）。

### 3. `te.c` / `fb.c` 裸调用与 `pv2_presenter_notify_te` 的线程安全边界
查看 `src/presenter.c:292-298`：
```292:298:components/display/tools/esp_display_present_v2/src/presenter.c
void pv2_presenter_notify_te(pv2_presenter_t *p, uint64_t timestamp_us)
{
    if (!p || p->kind != PV2_PRESENTER_TE) { return; }
    const pv2_backend_event_t event = {.kind = PV2_EVENT_TE_EDGE, .timestamp_us = timestamp_us};
    pv2_te_backend.on_event(p->backend, &event);
    (void)notify(p, false);
}
```
- `pv2_te_backend.on_event` 内部只获取 `backend->lock`（自旋锁）更新 `latest_edge`，然后调用 `notify(p, false)`（内部锁 `event->mutex` 或给 FreeRTOS 信号量），不触碰 Presenter 的 `gate` 互斥锁或 `owner` 状态。这一点设计得非常干净，确实支持从独立事件任务安全调用。
- **唯一需要注意的是**：在 `te.c:624-635` 的 `te_release_receipt` 和 `fb.c:508-516` 的 `fb_release_receipt` 中：
  ```624:635:components/display/tools/esp_display_present_v2/src/backend/te.c
  static esp_err_t te_release_receipt(pv2_backend_t *backend, pv2_receipt_t receipt)
  {
      if (!backend) {
          return ESP_ERR_INVALID_ARG;
      }
      (void)pv2_te_service(backend);
      te_record_t *record = find_receipt(backend, receipt);
      if (!record || !record->terminal) {
          return ESP_ERR_INVALID_STATE;
      }
      record->token = 0;
      return ESP_OK;
  }
  ```
  注意 `gram_release` 是在 `pv2_lock_enter(&g->lock, false)` 保护下检查 `record` 并清零 `record->token = 0` 的；而 `te_release_receipt` 和 `fb_release_receipt`（以及 `fb_cancel_frame`）在查找和修改 `records[]` / `slots[]` 时**没有加 `backend->lock`**。
  - 虽然任务侧 API 由单任务（或 Presenter `gate`）串行化，但**ISR 回调（`te_dispatch` / `fb:dispatch`）在 `PV2_EVENT_FAULT` 时会在持有 `backend->lock` 的情况下读取 `backend->records[...].token` 并修改 `record->result`**（见 `te.c:56-60`、`fb.c:57-60`）。
  - **建议**：在 `te_release_receipt`、`fb_release_receipt`、`fb_begin`、`fb_end_ex`、`fb_cancel_frame` 修改 `active` / `pending` / `records` / `slots[i].state` 时，保持与 `gram.c` 一致，用 `pv2_lock_enter(&backend->lock, false)` 保护，消除任务侧与 ISR 故障记录器之间的微小数据竞态窗口。

### 4. M2C Cache Invalidate 的对齐约束（针对下一阶段阶段 6：DMA2D / PPA）
- 当前 `lcd_spi.c` 使用 `esp_cache_msync(..., ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED)` 做外设 DMA 读取前的 Writeback，C2M 方向允许 `UNALIGNED`。
- 后续在 `pixel_accel.c`（DMA2D / PPA）中对目标 buffer 执行 **M2C（Invalidate）** 时，ESP-IDF `components/esp_mm/esp_cache_msync.c` 严格拒绝 `M2C + UNALIGNED`。届时必须按目标芯片/内存区域的实际 cache line 大小和缓冲独占范围做对齐校验，不能无条件外扩 invalidate 破坏相邻 CPU 脏数据。

---

## 四、 多中断与流水线取消的复杂度分析及架构护栏

当前 V2 已通过“任务侧 `service` 泵送 + ISR 仅写邮箱并唤醒 + Presenter `gate` 串行化”攻克了 TE 与 FB 的核心状态机。进入阶段 6（接入异步像素拷贝 DMA2D/PPA 与真实 RGB/DSI 驱动）时，需继续坚守以下四条工程护栏：

| 护栏原则 | 具体工程约束 | 落实状态与后续要求 |
| :--- | :--- | :--- |
| **1. ISR 只记账 + 唤醒，绝不推下一步业务** | 任何 ISR（Pixel / TE / Panel / VSYNC）在锁内只改计数器与邮箱位（如 `--pending`、`done = true`、`confirmed = true`、`latest_edge`），绝不在 ISR 内发起 repair 或调用 `panel.submit`。 | **已在 `gram.c` / `te.c` / `fb.c` 落实**。后续接入异步 Pixel Engine ISR 时同样只更新 ticket 状态并 signal event。 |
| **2. 单一不可逆分界线（Drain-on-Cancel）** | 未触碰硬件（`LEASED` / `BUILDING` / `QUEUED`）可立即撤回；一旦进入 `READY`（已 `end`）或触碰硬件（`SUBMITTING` / `INFLIGHT` / `PENDING`），禁止中途强杀共享硬件，统一通过 `complete` 或 `quiesce` 排空收尾。 | **已在三大后端落实**。后续 `te_composer` 引入异步 tile copy 时，需增加 `COPY_DRAIN` 阶段等待 in-flight copy ticket 终结后再释放 tile。 |
| **3. 硬件时序怪癖关进 `platform/lcd_*.c`** | 如 `lcd_spi.c` 屏蔽 SPI 分块入队一样，后续 `lcd_rgb.c` / `lcd_dsi.c` 负责封装双核 IPC 提交、bounce buffer 与 VSYNC 影子寄存器确认，向 `fb.c` 只输出标准的 `PV2_EVENT_SCANOUT_CONFIRMED`。 | `lcd_spi.c` 已完成；真实 `lcd_rgb.c` / `lcd_dsi.c` 待阶段 5/6 落地。 |
| **4. 停止生命周期的双重屏障（Worker Join -> Quiesce）** | 有后台任务时，必须先停止并 `join` 后台发送任务，再调用 `panel.quiesce` 等待在途 ISR 退出并解绑驱动回调；外部事件源（如 TE GPIO ISR）必须在 `destroy` 前由调用方注销并等待回调退出。 | **已在 `presenter.c` 与 `lcd_spi.c` 落实并经阻塞注入测试验证**。 |

---

## 五、 命名与代码风格跟进评估

对比上一轮 Review 意见，当前代码库在命名与风格上的最新状态如下：

### 1. 已改进项
- `gram.c`、`te.c`、`fb.c` 的内部生命周期枚举与 ops 表函数命名（如 `te_get_caps`、`te_submit_region_ex`、`te_release_receipt`、`te_on_event_isr`、`fb_get_caps` 等）已高度统一。
- `te.c` 已提取具名常量枚举 `enum { TE_MAX_BUFFERS = 16, TE_MAX_FRAMES = 32, TE_MAX_DAMAGE = 32 }`，且 `te.c` 全面采用了多行大括号展开风格，可读性与 GDB 可调试性明显提升。

### 2. 仍保留的机械整理项（可在功能冻结后单独提交 Format PR）
1. **单行多语句展开**：`gram.c`、`fb.c`、`presenter.c`、`present_event.c` 中仍保留了较多紧凑的单行 `if (...) { ...; return ...; }` 写法，后续可按 `te.c` 的多行排版风格统一展开，便于硬件 GDB 单步调试与覆盖率统计。
2. **公共头文件函数指针形参名**：`include/present_backend.h` 与 `include/present_presenter.h` 中部分函数指针/原型（如 `create`、`get_caps`、`begin` 等）省略了参数名，建议补齐以增强 IDE 参数提示。
3. **跨文件常量统一**：将 `gram.c`（硬编码 `16`/`32`/`64`）、`fb.c`（`FB_LIMIT = 3`, `RECORD_LIMIT = 32`）与 `presenter.c`（`receipts[32]`）中的上限常量统一收敛或对齐注释。

---

## 六、 阶段演进核对记录（Changelog Summary）

- **2026-10-29（阶段 1 ~ 4 初版）**：完成 CPU 像素引擎、GRAM 后端、SPI Adapter、`te_composer` 与 `te.c` CPU 后端；补齐 Sticky Fault 准入拦截与回调退出屏障。
- **2026-10-29（Presenter / LVGL -> TE 集成）**：完成 `presenter.c` 与 `present_lvgl.c` 对 TE 的接入，确立有界等待预算与超时暂停策略。
- **2026-10-29（阶段 5：FB 后端与全模式矩阵）**：完成 `fb.c`（1..3 FB，`PARTITION`/`FULL`/`DIRECT`，外部 FB 校验，最新封帧基线，单 FB 显式撕裂授权）及 `pv2_present_mode_t` 模式矩阵预设。
- **2026-10-30（事件化等待与可选后台调度）**：新增 `present_event.c`（Host `CLOCK_MONOTONIC` 条件变量 / FreeRTOS 静态信号量与互斥锁 + Worker Join），完成 Presenter 等待路径去轮询化与 TE/FB 可选 `background_service`；Host 14/14 测试（含真实 LVGL 9.5 三大后端渲染与 ASan/UBSan）及 ESP32-S3 含 LVGL 交叉编译全部通过。

- **2026-10-30（异步 copy 迁移）**：pixel FIFO worker/事件 wait/cancel 与 TE COPY_DRAIN 已落地；完成通知只唤醒 owner/sender，tile 不随 submit 返回立即复用；stop 先 join copy 再 quiesce panel。补齐 producer 交接与池/数量查询。普通和 ASan/UBSan 14/14，相关四组重复 100 次；泄漏检测关闭，未上机。仍需 FB/GRAM 异步、真实硬件加速与 scanout adapter、旋转和 tracking、LVGL 零拷贝。以上不表示本报告所有建议均已处理。

- **FB PARTITION 异步迁移**：补齐 COPY_DRAIN/CANCELING、scratch 生命周期和基线发布顺序；Host 延迟执行、错误、取消、stop 及真实 LVGL 图像对照通过。14/14 普通和 ASan/UBSan，泄漏检测关闭。基线预填/repair 仍同步，未作硬件时序验证。

- **GRAM 异步迁移**：COPY/READY 和按序 service 已落地，拒收后保留已接受像素，禁止后块超车；可选 Presenter sender 和真实 LVGL 路径已通过 Host。同步 in-place 拒收回滚保持原契约，异步配置明确拒绝 in-place。普通/ASan/UBSan 14/14，相关两组各重复 100 次；仍未上机。


## 第二轮板测补充（2026-10-30）

S3 RGB 与 P4 MIPI：DOUBLE_FULL、TRIPLE_FULL、DOUBLE_DIRECT、DOUBLE_PARTIAL、TRIPLE_PARTIAL 的同步 CPU 基线及 stop/recreate 通过，逐帧整图对照、回执回收、销毁后 heap 与用户目视确认均通过。详见 [RESULTS.md](test_apps/boards/RESULTS.md)。本次三缓冲为逐帧等待，尚不证明第三缓冲使用或排队背压；LVGL/旋转/加速器仍待实板验收。

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

## Executor插件代码切片验收（2026-10-30）

独立CPU/PPA/DMA2D执行器已落地，能力与probe可被上层查询；硬件插件不内调CPU，公共dispatcher仅在probe明确不支持时选择fallback。旧accel API薄包装保持兼容，类型分支仅在创建处，SoC宏限插件编译边界。各插件保留独立实例串行化和驱动资源仲裁；销毁失败保留对象。

普通Host+LVGL及ASan/UBSan 25/25（泄漏检测关闭）；四种插件集合分别链接，未启用硬件不提供mock符号。S3/P4离线构建通过。P4实测32例全内存对照通过：PPA16、DMA2D4、显式CPU回退12。原CPU显示五模式也完成一轮240帧。详见板测RESULTS与executor-summary.json。未修改GRAM/TE/FB生命周期或缓冲拓扑，不算S0/整体架构迁移完成。


## S0/S1 初始切片记录（已由本轮整体推进更新）

- `present_plan.c`：纯规划不调用 probe/硬件；格式、旋转、池计数、overflow、外部 FB 范围、对齐与像素预算检查。未实现运行拓扑在 create 前拒绝。
- `present_frame_core.c` / `present_transaction.h`：GRAM 共用帧核心和接受性归并。封帧前完成不回收记录；外部 receipt 终态仍需释放；取消/closing 必须等待 pending 排空。GRAM cancel 的任务侧完成历史检查也已加锁。
- `test_plan.c` / `test_frame_core.c` 加上既有 GRAM/Presenter/LVGL 回归：27/27 普通和 sanitizer 通过；泄漏检测关闭。S3/P4 编译通过。
- **仍需继续**：公共 allocation 引用和 lease 操作、S2 history/repair 构建器、TE/FB 接入、LVGL 对齐协商、完整总内存预算。不因接口/测试存在就将这些功能标记完成。

## 驱动扫描边界专项（2026-11-02）

新增独立 [scanout_boundary 工程](test_apps/scanout_boundary/README.md)：仅在隔离构建副本中给 IDF/V2 注入事件记录，生产接口和运行策略不变。记录源读取区间、每次 bounce EOF、FB 完成、VSYNC、链表配置/切换、restart，以及 V2 提交/确认/退休/构建。溢出、截断、watchdog 或关键证据缺失不得判通过。

本轮 Host + LVGL 与 ASan/UBSan + LSan 为 **31/31**（含 C 记录器测试和 Python 分析器测试，后者有 15 个用例）。实板结果与边界以 [专项 RESULTS](test_apps/scanout_boundary/RESULTS.md) 为准，不把单次画面正确当作生命周期证明。

支持范围保持连续 RGB bounce / MIPI；无 bounce 连续/单次 RGB 只观察 IDF，不接入 V2。默认双边界确认的结果不外推到 single_boundary_retire。仍未受控注入丢 EOF / MIPI underrun；MIPI 驱动原生 underrun 仅日志，生产 FAULT 转发/恢复尚无本轮证据。记录器改变时序，也不证明光学显示完成或无撕裂。

## 短期收敛 C0（2026-11-02）

已修正 frame_core 在 supports_present 时无条件报告 source_released 的问题，改为保守帧完成且 pending/lease 排空；不是新增提前释放阶段。新增 gated async copy + failure + stop timeout + Presenter receipt 回归。Host/LVGL、ASan/UBSan/LSan 均 31/31，四个相关测试各连续 20 次通过，S3/P4 离线编译通过，未烧录。后续按 [CONVERGENCE_PLAN.md](CONVERGENCE_PLAN.md) 执行，真实兼容面见 [V1_API_AUDIT.md](V1_API_AUDIT.md)。平台工厂、V1 门面与无位图路径仍未完成。

## C1 基础资源层（2026-11-02）

新增私有资源聚合而非应用新 API：CPU/可选硬件创建、驱动 FB、默认 tile/service、失败清理与逆序销毁。新增 retained-driver-scanout 停止路径，只保留驱动自有外部 FB，先排空 sender/copy 与回调；原默认 stop 不变。新 presenter 在第一次 producer lease 前显式建立 FB0，避免复用借用 panel 时猜测初始源。原始 VSYNC 观察不参与退休 fence。详情和未覆盖边界见 ../esp_display_present/README.md。

C1 时点 C2 同名门面尚未实现（当前进度见下文）；TE/单 FB 等明确拒绝，不能按已迁移计。新增 native IPC join 尚无本轮实板时序证据；超时保证也受原生同步 API 限制。

C1 本切片最终检查：32/32 Host/LVGL 与 32/32 ASan/UBSan/LSan，两个新增/扩展生命周期测试各 30 次通过；S3/P4 强制资源层链接的离线 smoke 通过。取消注册时旧函数配空 user_ctx 的交错已做空指针防护和回归。旧 V1 头文件未改布局；本轮无实板烧录。

## C2 基础兼容入口进度（2026-11-02）

21 个 V1 同名入口、旧 LVGL start/stop/frame_count、32/64 位 lease 映射及私有控制桥已接通。资源聚合归入兼容组件，核心不反向依赖 V1。新增 legacy_facade Host 回归与原工程离线准备工具；原默认组件未切换。六种 FB 模式、tracking/generation/scroll、控制任务和实际 LVGL handoff 已有基础 Host 证据。

C2 尚未整体完成：保留 TE/单 FB 等明确拒绝项，逐笔传输 timeout 尚不等价；原工程/GSP 完整验收仍按 [收敛计划](CONVERGENCE_PLAN.md) 继续。C3 新固件板测、C4 常驻 coverage 位图收敛、C5 条件性优化未完成，不计入已有驱动板测。

本轮 C2 基础切片最终结果：Host/LVGL 33/33、ASan/UBSan/LSan 33/33（泄漏检测开启）、legacy_facade 连续 50 次；S3/P4 组件离线 smoke 及三个原示例的 P4 离线链接通过。showcase 使用缓存 GSP 1.9.0，不等同于原 1.5 发布组合。未烧录、未跑分；详情见 [C2 RESULTS](test_apps/legacy_compat/RESULTS.md)。C2 整体仍未验收完成。

## C2 第二轮收敛（2026-11-02）

已按 V1 源码修正帧计划、band 行数对齐、GRAM 水位、tracking 与 fault/handoff 语义；直接链接原纯几何实现进行 Host 差分。逐笔传输期限放在现有 Presenter 提交/事件入口，启用才分配计时记录；记录不拥有像素，不改后端退休规则，也不新增线程。超时只报 FAULT，真实完成仍须排空。

Host/LVGL 与 ASan/UBSan/LSan 均 34/34，三个相关回归各连续 50 次；S3/P4 组件、三个原示例 P4 离线重新链接及格式/补丁检查通过。无烧录、无新跑分。TE 平台模型/内存事实/事件源、单 FB、非方形旧 API 运行时跨轴、实际 GSP 发布组合和 C3～C5 尚未关闭，详见 [最新 RESULTS](test_apps/legacy_compat/RESULTS_C2_CONTRACTS.md)。

C3 P4 原 GSP/LVGL 交接已完成一整批 5 轮及额外第 6 轮，日志零错误；每轮 LVGL 139 帧，未做官方 benchmark。修正并回归了保留 LVGL 绑定但暂停 raster 时的 quiesce 排空。详见 [P4 交接结果](test_apps/legacy_compat/RESULTS_C3_P4_HANDOFF.md)，三板完整 C3 与 C4/C5 仍未关闭。

P4 交接视觉反馈更正：用户报告仅背光亮、无显示。6 轮串口 PASS 仅证明日志层交接，不能标为显示验收通过；C3 保持未通过，需驻留测试固件并同步排查面板输出。详见交接结果的视觉反馈更正。

## 门面的能力超集原则（2026-11-02）

门面以 V1 已成功调用的契约为底线，向上开放已验证的 V2 能力。旧成功调用的逻辑尺寸、像素、所有权和生命周期语义不变；以前失败的配置可以新增支持。PV2_PLAN_V1_DEFAULTS 选择默认模式/池数量，不充当 V1 能力上限。

已移除 GRAM、DIRECT 非零旋转的纯 V1 限制，联动 buffer-count 查询、资源创建、plan 和 set_rotation。初始非方形旋转保持原配置的逻辑宽高，物理输出按角度映射；运行时非方形跨轴仍拒绝，不会静默交换旧调用方的逻辑宽高。GRAM 四方向及同轴运行时二次提交、DIRECT 四方向及同轴二次提交均以真实 V1 几何函数做逐像素 Host 对照。

真实资源限制仍由核心检查：例如 0° 原地字节交换 GRAM 实例不能运行时切换非零角度，失败保留 generation。需旋转的调用方可在创建时指定角度以获得转换路径；本轮不为原来成功的 0° 配置强行增加转换池。TE 平台装配、单 FB、RGB 无 bounce、非方形运行时跨轴等缺口继续明确记录，不冒充超集已全部实现。

本轮仅 Host 与离线编译，不烧录、不改默认示例选择；之前 P4 黑屏/复位观察仍独立待定位。完整结果见 [超集旋转验收](test_apps/legacy_compat/RESULTS_SUPERSET_ROTATION.md)。

V1 取消契约收敛：void cancel 对外立即撤销 frame/lease，内部保留取消排空；下一次 begin 共用准入预算，超时可重试且不提前释放源。独立控制任务只能处理门面已撤销的 producer frame。见 [取消回归](test_apps/legacy_compat/RESULTS_CANCEL_COMPAT.md) 与 [V1 成功路径缺口](V1_COMPATIBILITY_GAPS.md)。本轮不是整体兼容完成，显示黑屏反馈与创建/旋转缺口仍未关闭。
