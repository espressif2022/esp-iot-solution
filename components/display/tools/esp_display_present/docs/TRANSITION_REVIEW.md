# 生命周期切片独立复审（2026-11-03）

这是较早代码快照的复审，文中“五个公开头保持一致”等结论不适用于后续提交；当前几何接口变更和测试状态见 [当前验证](../dev/VALIDATION.md)。

**最终结论：** 首次复审提出的有效故障快照帧号丢失、早到 fence 测试竞争、故障后 tracking 水位三项均已修复并补 Host 回归。下文“需要处理”为保留的历史发现，不代表当前仍未修复。最终三目标 IDF 编译和符号核对见文末；真实屏幕时序仍需实板验证。

本报告针对命名迁移前的 T0/T1/T2 工作树，仅静态复审生产代码与既有测试日志。未重新上板，未证明完整并发穷举；命名迁移进行中，后续需要以最终源码和重新构建结果复核。

## 需要处理

1. **提交成功后立即发生故障时，旧 receipt 帧号可能错误。** `observe_core_timeline()` 仅在 `pv2_presenter_control_get_status()` 返回成功时更新 `last_submitted_frame`，而 `get_status_impl()` 在 `reap_ready()` 出错后仍填入包含 `committed_frames` 的有效状态快照。`commit_frame_with_receipt()` 忽略观察错误，使用旧水位构造成功的 receipt。故 commit 已接受而状态观察遇到异步故障时，可能返回旧帧号或 0；拆分前无条件投影该字段。需定义控制接口“返回错误但快照有效”的契约，并补受控故障回归；不能简单把已接受的 commit 改报失败，以免误导调用方重复提交。
2. **早到终态回归的事件注入仍有竞争。** 模拟驱动在 `draw_bitmap` 内更新 `panel.calls` 并唤醒等待方；scanout 在 `draw_bitmap` 返回后才将 cookie `armed=true`。测试 wrapper 等待 `calls` 后立即注入两个边界，若抢先于 armed，事件可能被忽略，导致偶发失败。可在注入前通过独立核心状态查询穿过 presenter gate，确认 submit 已返回；或使用明确的受控 armed 握手。不要用 sleep。现有 30 次通过不能证明此窗口不存在。
3. **故障后 tracking 发布规则须确定并补测试。** `poll()` 在投影核心 `first_error` 后仍收集及发布成功的 tracking；若先前帧故障与被跟踪帧成功同时存在，可能在门面 FAULTED 后更新 `confirmed_present_frame`。拆分前也有此结构；设计文档要求故障后不发布成功确认。需要先根据旧契约确定同一时刻并发结果的处理，再设计故障交错用例，不应仅以字段值推断。

## 本次改动的已验证部分

- 条件变量让独立线程实际持有核心 gate；首次零预算 void cancel 无法进入核心，逻辑旧 lease 立即失效。释放 gate 后下次 begin 完成撤销且无协议故障。实际测试没有通过模拟取消返回值代替锁竞争。
- `cancel_pending` 是门面尚未送达/排空的意图；核心 cancel_frame_impl 负责执行排空，成功才清理核心帧。stop 针对无租约帧先逻辑撤销并排空；已交出 lease / LVGL 绑定继续拒绝；失败可重试。已有测试涵盖多后端未封帧 stop、lease、异步 copy、DIRECT prefill 和控制线程排空。
- `collect_tracking_receipt` 对成功 fence 只在 `confirm_after==0` 建立阈值，release 失败不清 receipt；`publish_tracking_watermark` 要求 receipt 已释放后才发布。回归注入一次 release 失败，跨一个原始 VSYNC 成功重试，在第二个 VSYNC 确认，能抓住重复重设阈值；它尚未单独覆盖 release 连续失败直到两个 VSYNC 后仍不得发布的情况。
- 早到测试通过真实 scanout 回调、真实核心 receipt 查询与旧 API 观察确认水位，能够抓住登记旧帧号前先释放 receipt 的问题，但需先修第 2 项竞态，才能视为可靠回归。
- `PV2_HOST` 包裹 test gate 定义及声明，Host CMake 显式定义。IDF 分支的 CMake 在创建组件后 return，未加该宏；既有 P4 IDF `compile_commands.json` 对 presenter.c 无 `PV2_HOST`。该编译数据库产生于新增 hook 之前，不能代替最终候选的离线重编译与符号核对。

## 证据与边界

- `/tmp/pv2-lifecycle-host.log`：Host + 实际 LVGL 35/35。
- `/tmp/pv2-lifecycle-san.log`：ASan/UBSan/LSan，`ASAN_OPTIONS=detect_leaks=1`，35/35。
- `/tmp/pv2-lifecycle-repeat.log`：legacy_facade 连续 30 次通过；不构成调度穷举。
- T0 字段归属已设计并对关键状态路径作审计，但本报告所列错误优先级/故障交错仍要补齐。
- T1 锁竞争与取消意图已有针对性 Host 回归；硬件停止时序仍需候选板测。
- T2 结构拆分和主要 tracking 修复已有 Host 证据，但第 1、2 项修复和第 3 项契约裁定之前不能标为完全完成。

## 修复后复核（命名迁移前）

- 项 1 已处理：新增私有 `get_status_snapshot(..., valid)`，在获得 gate 并调用保证填充输出的 `get_status_impl` 后才置有效；有效故障快照仍推进 committed 身份。测试通过包装私有控制接口模拟“快照有效且报告执行故障”，验证成功 commit 的 receipt ID。无效快照不会覆盖旧水位；这是一次状态读取约定，不代表已覆盖所有可能的底层锁失败场景。
- 项 2 已处理：早到查询 wrapper 在 panel.calls 后通过核心状态调用穿过 gate，sender 的 scanout submit 完成并 armed 后再发送边界。依赖 sender 在提交过程中持有核心 gate；代码实际满足此条件。
- 项 3 已定案并处理：门面已锁存 fault 后既不建立新的确认阈值也不发布先前已建立的阈值，terminal receipt 仍及时回收。两组测试覆盖 fence 前和 fence 后故障：真实 scanout boundary 前原始 VSYNC 不触发回收，boundary 后可回收；这不替代真机 DMA/scanout 时序。
- 更新后 `/tmp/pv2-lifecycle-host3.log` 与 `/tmp/pv2-lifecycle-san3.log` 分别 35/35，后者 leak detection 开启。诊断故障为控制读取 wrapper 注入，不是硬件真实故障。

## 命名及组件迁移后的只读边界审计

- `esp_display_present` CMake 编译一个 `src`，旧门面 `resources.c` 调用同一 `esp_display_present_engine_create_planned`；`esp_lv_present` 编译旧应用 binding 与 native binding，两者共享同一 Presenter 引擎。两个生产 manifest 不再依赖 `_v2` 组件。此判断针对当前 CMake/source，尚待最终 IDF 链接 map 核对。
- 核心和 LVGL 的 advanced `present_*_pv2_compat.h` 明确写明仅源码兼容，不提供旧 pv2 ABI 符号；核心 compat 的每个简单宏目标都能在非 compat 公开 advanced 头中找到。未见 image 实现、frame core、presenter_control、test gate 或私有 backend_ops 的别名；public backend 配置/适配器保留，backend ops 定义位于 private src 头。
- 五个稳定旧头：`esp_display_present.h`、`esp_display_present_config.h`、`esp_display_present_types.h`、`esp_display_present_geometry.h`、`esp_lv_present.h`，迁移后与当前 HEAD 按字节比较相同。头一致不证明所有运行行为或 ABI 二进制布局在不同编译选项下完整兼容。
- test_gate 仅声明在 `src/presenter_control.h` 且定义和声明都受 `PRESENT_HOST` 保护，未见公开头导出。最终 IDF 产物需确认宏不启用、符号不出现；该验证由构建代理进行。


## 最终 IDF 产物核对

最终 S3、P4、S31 的 `compile_commands.json` 各有一份 `src/presenter.c`，无 `PRESENT_HOST` 宏，无旧 `esp_display_present_v2`／`esp_lv_present_v2` 组件路径。各自 `libesp_display_present.a` 的 `nm` 输出均无 `presenter_test_gate` 符号，各有一份 `esp_display_present_engine_create`、一份 `esp_display_present_engine_create_planned` 定义。这是编译单元及归档符号核对，不是上板时序或所有二进制兼容验证。构建目录：`/tmp/present-component-audit/idf-s3`、`idf-esp32p4`、`idf-esp32s31`。


## 2026-11-03 后续交付复审

1. **TE GPIO 越界：已修复。** `src/present_te.c` 在读取引脚或构造 `1ULL << gpio` 前检查负值、64 位掩码上界以及 `GPIO_IS_VALID_GPIO`，非法引脚返回参数错误。新增 -1、S3 保留引脚 22、49 和 64 的 Host 回归，真实 TE 单元与 ASan/UBSan 测试通过。
2. **旧 LVGL 版本回归：已修复。** 组件原允许 LVGL 9.x，新实现先前将最低版本提高到 9.5。现在恢复 `>=9,<10`，9.0–9.4 的旧应用通过独立绘制缓冲与复制路径回退；高级 pooled / FULL 零拷贝在这些版本明确不支持。LVGL 9.0、9.1、9.2、9.3、9.4、9.5 的 Host 结果及旧 API FB FULL/DIRECT 专项见 [结果](../dev/RESULTS.md)。9.0/9.5 sanitizer 已运行；该结果不证明全部 9.x 未来版本和真机时序。
3. **组件本地 override：已修复。** `esp_lv_present` 的 manifest 不再硬编码兄弟组件 override，实际 compote 归档分置的本地消费者构建通过。用户应用应同时指定新 presenter 和 LVGL 本地路径，详见[打包结果](../dev/PACKAGING_RESULTS.md)。
4. **Registry 版本耦合：未解决。** 新 LVGL 代码引用新 presenter 的内部桥，而现有依赖 `^1.1.2` 会允许旧 Registry presenter；目前两个组件版本号也与旧包重复。实际发布前必须确定版本策略、提升依赖下限并验证 Registry 真实解析。不发布或把本地 path 消费当成线上解析已通过。

本次未形式化穷尽多线程或所有硬件路径；实板结果、视觉确认和 RGB GPIO3 警告见后续板测记录。旧 SDK 缺少 RGB 能力查询时的安全拒绝与配置选择、连续无 bounce 多 FB、按需刷新等仍保留各自边界。
