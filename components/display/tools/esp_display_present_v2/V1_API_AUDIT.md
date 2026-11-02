# V1 调用面与兼容门槛

本轮读取当前工作区真实头文件、示例、LVGL 绑定，并对 GSP 1.9.0 的 S3/P4/S31 预编译库执行 `nm -u`。符号及输入 SHA256 见 [V1_API_AUDIT.json](V1_API_AUDIT.json)。这是实现前的核对表，不是已完成兼容的声明。

## 已确认的消费者

| 消费者 | 已核实的依赖 |
| --- | --- |
| lvgl_present_benchmark | presenter create/delete/quiesce；LVGL start/stop/frame_count；板级 helper 负责 framebuffer 数量查询 |
| gsp_lvgl_present_handoff | presenter 创建、GSP 暂停/恢复、LVGL 生命周期、quiesce；真实渲染调用位于预编译 GSP |
| gsp_showcase | hw helper 与 `esp_gsp_esp_lcd_start` 间接创建/使用 presenter，不能只扫描 main.c |
| esp_lv_present | caps、drawbuf、alignment、rebind、begin/acquire/submit/commit/cancel |
| GSP 1.9.0 S3/P4/S31 库 | 三平台均依赖同一组 14 个 V1 presenter 符号；并声明 esp_display_present ^1.2.0 依赖 |

GSP 核心不是可在本仓库随意重编的普通 C 调用方。符号存在还不足以证明二进制兼容：结构体大小/偏移、函数签名、callback ABI、返回语义、IDF/工具链必须另验。当前头文件 SHA 固定的是本工作区版本，不是已证明等于预编译库构建时版本。

## 逐接口映射

下表省略统一的 `esp_display_` 前缀，`presenter_*` 均为 V1 公共符号。

| V1 API | V2 可复用基础 | 门面必须补足 |
| --- | --- | --- |
| present_get_required_frame_buffer_count | get_required_frame_buffer_count / plan resolve | 名字不是 presenter_get_required；V1 AUTO、TE、旋转查询规则 |
| presenter_create | create_planned + adapter/executor | 资源工厂、逆序失败清理、目标/回调所有权、创建前拒绝不支持配置 |
| presenter_stop | stop(timeout) | 原签名与旧停止协议；超时保持实例，不能提前 free |
| presenter_delete | stop + destroy | 重试、观察者释放；借用 panel 不能无条件删除 |
| presenter_set_rotation | set_rotation | 排空/回滚及逻辑区域重新协商，原实例地址不变 |
| presenter_get_caps | get_caps | contract、retains_content、previous_surface_readable 的真实能力，不能凭枚举推断 |
| presenter_get_drawbuf | get_draw_buffers | V1 最多两个指针、容量/行数；裸池指针不等于当前 lease |
| presenter_get_buffer_alignment | caps/plan | 保留原幂次对齐语义，旋转后重新查询 |
| presenter_rebind_producer | rebind_producer | 旧 producer 确实退出；已封帧工作可继续；不自动转移 LVGL 实例 |
| presenter_get_state | get_status | OPEN/CLOSING/STOPPED/FAULTED 的原语义，不在原对象清除 FAULTED |
| presenter_get_fault_reason | backend fault/status | V1 细分原因与 V2 error/cookie 映射，不能都改成通用 ESP_FAIL |
| presenter_map_point | map_point | V1 参数宽度及旋转逻辑空间 |
| presenter_begin_next_frame | resolve_render_area + begin_next_frame | 返回 render_areas/out_full；首帧/旋转失效；has_scroll 不可静默忽略 |
| presenter_acquire_buffer | acquire_buffer | V1 surface/capacity、32 位 lease_id、resolve_rows callback trampoline |
| presenter_submit_buffer | submit_buffer | inclusive area、stride、lease 映射校验，不能把 64 位 token 直接截为 32 位 |
| presenter_commit_frame | commit_frame | V1 coverage 类型与封帧失败规则 |
| presenter_commit_frame_with_receipt | commit_with_receipt | track_display、frame_id/generation 和显示证据；不能把任意终态当作已显示 |
| presenter_get_frame_status | query_frame + 门面状态记录 | 原 API 无单独 receipt 参数；重复查询和 generation 一致性，不在查询时偷偷回收 |
| presenter_cancel_frame_tracking | cancel_tracking | 精确匹配 frame_id/generation；只结束观察，不释放硬件或伪造显示完成 |
| presenter_cancel_frame | cancel_frame | 原 void 接口；异步排空/取消重试需要留在门面，不能丢弃 pending 状态 |
| presenter_quiesce | drain | 暂停后可继续，绝不能用永久 stop/delete panel 代替；调用任务语义需与 handoff 核对 |

LVGL 另有三个公共符号：`esp_lv_present_start/stop/get_frame_count`。门面内部应复用现有绑定；start 的初始化/tick/rebind 与 stop 的取消/排空必须匹配原调用方。共享 tile 不等于直接注册两个未经 acquire 的裸缓冲，fallback 不在本阶段删除。

## 需要先决定的兼容语义

- **scroll**：V1 请求含 viewport/dx/dy，V2 frame request 无相同字段。可按原契约要求整帧重新渲染，但必须在 raster 前通过 out_render_areas/out_full 告知；不能渲染完才扩大区域。
- **tracking**：V1 的 track_display、frame_id/generation 和取消观察不是简单的 V2 receipt cast。门面保持自己的状态记录与回收策略。
- **线程**：V2 多数 task API 限创建/绑定任务；V1 的 const 查询、quiesce 和控制线程调用要结合 GSP pause/handoff 实测，不直接逐函数转发。
- **资源所有权**：正常 quiesce 要保留可用 panel；最终关闭必须证明 ISR/扫描停止。共享 panel 的 callbacks 与 bus 不能被门面错误接管或双重销毁。
- **TE**：V1 使用 GPIO/测量流程；V2 窗口需要明确的时序模型。先迁移已确认流程，不能凭一个默认周期伪造兼容。
- **fault**：已接受访问出错后仍需保留缓冲直到排空；不能通过 CPU 回退覆盖或清 sticky fault 来制造“可恢复”。
- **链接**：V1 和同名门面实现必须互斥选择，禁止同时导出两套同名符号。先固定公共头文件与选择方式，再写平台创建代码。

## 尚未通过的验收

- [ ] 门面实际实现和原头文件布局校验。
- [ ] 三示例只换组件选择、不改业务源码的离线编译。
- [ ] 三平台实际 GSP 预编译库链接/运行、tracking、scroll、handoff。
- [ ] 门面默认配置的官方 LVGL benchmark；不得沿用手工组装版本的跑分。

当前完成的是调用面审计和 C0 的核心状态修正；后续状态见 [CONVERGENCE_PLAN.md](CONVERGENCE_PLAN.md)。

## C2 当前增量

21 个同名入口及 LVGL 旧入口已实现，原头文件未改布局。Host 与 P4 原工程离线链接结果见 [C2 RESULTS](test_apps/legacy_compat/RESULTS.md)。这只更新实现/构建进度，保留本审计的 ABI/行为验收门槛，不因符号或链接通过而标记整体兼容。

第二轮已进一步对齐 full/count、参数校验、scroll、row alignment/UINT16 行数、GRAM completed_present 和 tracking/fault/handoff；参考原 V1 geometry.c 做差分。非方形旧 API 运行时跨轴保持显式未支持，不把原生 V2 固定物理画布的行为冒充 V1 固定逻辑形状。见 [最新结果](test_apps/legacy_compat/RESULTS_C2_CONTRACTS.md)。
