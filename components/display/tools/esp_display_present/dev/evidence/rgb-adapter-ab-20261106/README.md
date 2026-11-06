# S3 RGB present / adapter 同配置对照 · 2026-11-06

已先本地提交上一轮已验证修改：`4e050d4be`。本轮只补齐测试对照校验与证据，未改生产缓冲交接、IDF 或缓存同步。

S3 LCD EV R16 / GC9503 480×480 RGB，MAC `90:e5:b1:d0:a9:d8`，bounce 10行、driver 3帧缓冲。干净 IDF `7b9cc1ac`，LVGL 9.6.0~1、CPU 240MHz、PSRAM 80MHz、32KB I-cache、PSRAM XIP、2个软件绘制单元。双方统一测试主程序，均未开启分段统计，仅测试0°；每项16个有效场景。

| 模式 | present FPS | adapter FPS | present render/flush | adapter render/flush |
|---|---:|---:|---|---|
| NONE | 41 | 51 | 16/11 ms | 15/3 ms |
| DOUBLE_DIRECT | 39 | 42 | 17/12 ms | 17/7 ms |

这是有效场景平均 LVGL FPS，不是整轮吞吐。adapter 未提供传输计数，日志的 frames=0/transport_fps=0 是不可用占位。单次结果不构成多轮置信区间或无撕裂证明；本轮未收到新的目视反馈。

## 已确认的路径差异

- adapter NONE 在 `display_manager_pick_render_mode()` 中选择 PARTIAL，使用50行分块绘制；present NONE 在 `present_resources.c` 中选择 DIRECT 持久画布。`esp_lv_present_start()` 对 DIRECT 创建普通 copy binding；`copy_flush()` 逐行 memcpy 脏区到 presenter lease，并在最后一次flush提交整帧。这是两种内部实现的模式对照，不是相同渲染路径的微基准。
- 上一轮 executor 次数为0，仅指没有进入 executor_run。它不覆盖 binding 内 memcpy，不能解释成整条路径零拷贝。缓存维护范围也不是实测总线流量。
- DIRECT 两者平均 render 同为17ms，差别主要表现在flush指标12 vs 7ms。但该指标含等待/复制等，不能直接断言是某一次IPC或全屏拷贝的成本。present 保留跨核 ISR join，不能为追分换成无事务对应的完成通知。
- 两者 LVGL 任务优先级均为6、无固定核。present 测试循环最大sleep 10ms，adapter 默认15ms且支持通知提前唤醒；调度并非完全等价，尚未做单变量调度实验。

## 本轮修复与验证

对照工具原先只检查部分启用配置，漏掉指令/数据缓存等选项。现在共同配置指纹包含芯片、缓存、LCD和显式禁用选项，并排序消除文本顺序影响；组件私有 Kconfig 不冒充共同配置。同时比较公共defaults与共享测试源码哈希。新增回归验证缓存/禁用项变化会拒绝对照、配置顺序变化不影响结果、共同源码/defaults变化会拒绝。

双方最终manifest的board/cases/IDF/LVGL场景/共同sdkconfig/defaults/共享测试源码逐项一致。adapter 刷入固件与重建后清单哈希一致；present 在跑完后独立 verify-flash 与最终构建bin匹配。Host 40/40通过，工具12/12通过。过程中一次使用python而不是该构建目录原配置python3，IDF在编译前拒绝，改用原入口完成，不涉及硬件失败。

## 后续顺序

1. 给DIRECT的binding拷贝、队列等待、worker实际提交与scanout确认分开计时，统计关闭后复测；当前平均flush不能唯一归因。
2. 检查GRAM/TE转换与发送重叠，维持tile/compose/staging生命周期；不把SPI DMA完成误作TE帧边界。
3. 旋转/转换只在确认重复搬运后处理；CPU旋转已融合字节交换。PPA strict-MSPI拒绝与IDF一致，不放宽。

本次不更改NONE渲染合同、不删除缓存同步、不缩短多缓冲安全确认。原固件恢复结果见restore日志及validation.json。

恢复已完成：原始16MB写回，独立整片verify-flash成功，SHA256 `a1fb70defbe454c7a5d8fc233929fd9d9d39751910336c7baa96b905b60730fc`，复位回原固件。
