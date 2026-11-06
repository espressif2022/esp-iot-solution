# 公共层与 Engine 状态归属 · 2026-11-06

本轮审计基于 `0b46be813`。字段名称相似不等于生命周期相同；本批只收敛完整租约描述副本，保留逻辑撤销、后台排空和观察时间线的区别。

| 状态组 | 公共层职责 | Engine / backend职责 | 本批结论 |
|---|---|---|---|
| producer / owner | 已接受的公共生产者会话 | 当前core调用者身份 | 保留。LVGL bind中core rebind与后续prepare/drain有不同提交点；合并前须单独定义失败回滚。 |
| active / frame | 公共帧是否仍允许生产者调用 | 实际backend构建和待排空工作 | 保留。void cancel先撤销公共帧，backend可能仍排空。 |
| cancel_pending / canceling | 立即置位的逻辑撤销门闩 | 进入core后才置位的排空状态 | 保留。core gate无法零预算取得时，两者可以有意不同。 |
| lease描述 / held | 公开32位ID到原生ID及地址的映射 | 完整租约、布局、容量、资源借用 | 已收敛：完整描述由engine保管。 |
| state | OPEN / FAULTED / CLOSING / STOPPED兼容状态 | RUNNING / CLOSING / STOPPED与sticky error | 保留。公开FAULTED不等价于core立即停止。 |
| timeline / completion | 过滤失败终态的成功水位、公开generation | 原生提交/终态计数与receipt状态 | 保留。terminal计数包含失败，不能直接作为成功完成水位。 |
| tracked / observers | 单个公开跟踪ID与generation及观察取消 | 原生receipt缓存，不授予像素写权限 | 保留。取消观察与取消传输独立。 |
| vsyncs / confirm_after | 兼容层跟踪确认水位 | cookie对应的实际扫描确认与旧FB退休 | 保留。公开观察水位不能替代硬件缓冲归还证据。 |
| config / caps | 公开配置与成功旋转后的查询快照 | 当前backend几何与能力 | 保留。需要明确查询/旋转失败边界后再缩减。 |
| full_next / render_alignment | 下一帧光栅协商及生产者行对齐要求 | 实际写入覆盖、历史补全及输出对齐 | 保留。协商要求不能冒充实际像素覆盖。 |
| gate / events | 公共控制序列化及VSYNC计数 | engine gate/control与worker事件 | 保留。沿用现有锁顺序，不跨层共享一个大锁。 |

## 本批改动

公共层原保存完整esp_display_present_lease_t及公开ID，现仅保存native_id / pixels / public_id。Acquire用临时原生lease构造公开返回值；行数解析、提交通过私有身份桥接，在原engine gate内构造临时校验视图，继续调用原resolve_rows_impl / submit_buffer_impl及valid_lease。

公开ID仍是独立单调32位计数，原生ID保留64位；同地址复用不能复用旧token。公共错误指针校验仍发生在几何校验之前。取消先清除公共映射，再沿用原排空路径，不能因为删除描述副本而提前复用像素。

没有修改公共头文件、owner提交点、状态机、停止次序、预算或扫描机制。Host下公共对象由432降到384字节；目标芯片对象尺寸及验证范围见[本批证据](evidence/lease-state-refactor-20261106/README.md)。

下一批应先检查生产者交接的提交点和失败回滚，再判断producer / owner能否收敛；不能只删除一个字段并改变失败后的会话归属。
