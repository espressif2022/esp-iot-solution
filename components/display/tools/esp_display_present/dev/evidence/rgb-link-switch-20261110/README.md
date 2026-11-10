# RGB 请求式切链交接修复（2026-11-10）

原问题：周期完成路径在draw返回后join ISR、清零并开始计数。S31非bounce完成来自按请求产生的link-switch，唯一完成事件若提前到达会被丢掉，随后一直等。前一轮原生等待模型的50ms窗口48/48次超时，保留提交前计数则0/48次超时；该模型不是完整present运行。

本轮修复在真实scanout适配器分离两种协议：

- RGB bounce及DSI保留周期完成的ISR join与失败时双事件保护。
- S31非bounce提交前建立单一事务；提交期间的完成先缓存，驱动成功返回后在同一把锁内认领且只发布一次。晚到完成由ISR发布。提交失败保留不确定状态，不归还旧缓冲，不再接受新提交。
- 新增target.hw.rgb.scanout声明，默认AUTO保持既有配置；干净IDF可明确选择BOUNCE或LINK_SWITCH。成功的驱动查询优先核对实际配置。S3非bounce EOF不能作为可靠退休事件，仍拒绝LINK_SWITCH。
- local cookie不赋予IDF回调地址/代次身份。必须独占面板与回调、只允许一个未完成切换；停止/重建需要原有detach及ISR join生命周期。

## 验证

| 路径 | 检查 | 结果 |
|---|---|---|
| Host + ASan/UBSan | 44项，含真实scanout早/晚完成、失败、重复通知、单事务、关闭重试、查询/无查询与验收矩阵 | 全通过 |
| S31 Korvo RGB 800×480 | 干净IDF，bounce=0，真实present六模式×四角度×提交core0/1×返回延迟0/50ms | 96项通过，384帧 |
| S3 R16 RGB 480×480 | 十行bounce，同一真实present矩阵 | 96项通过，384帧 |
| P4 DSI | 干净IDF smoke编译/链接 | 通过；本轮未上P4板 |

六模式为DOUBLE_FULL、TRIPLE_FULL、DOUBLE_DIRECT、DOUBLE_PARTIAL、TRIPLE_PARTIAL、AUTO，不含NONE。每项四帧，全量校验物理输出像素与边界；每项关闭重开presenter，复用同一个三FB面板，模式按策略使用两/三FB子集。单输入tile。面板中断创建在core0，生产者分别在core0/core1。

S31所有96项均观察到5次驱动完成（初始化FB0一次+四帧）。48项50ms延迟组总计240次早到事件，均完成，未等待第二次周期事件。S3延迟组也完成；其周期事件数不能解释为提交帧数或FPS。

50ms由测试链接wrapper主动插入真实draw返回到scanout之前，回调正常转发，没有IDF补丁或生产调试开关。**本轮检查像素内存和交接时序，没有目视防撕裂验收，不替代LVGL完整benchmark性能结果。** 生产S3非bounce未启用。

两板原固件均恢复并独立整片校验。原始flash备份在客户工作区hardware-backups/present-rgb-link-20261110，不进入组件证据。此目录只含源码/配置/日志/结果，不含build或固件镜像。IDF rev 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe，保持干净。逐项结果见两板results.json，恢复凭据见backup-restored.json及backup-restore-verify.log。

运行入口见 ../../test_apps/rgb_handoff/README.md。test-inputs按构建manifest哈希保存；测试main CMake在上板后仅增加两行SPDX注释，存档还原编译时内容，其余运行C/H源码与最终实现哈希一致。
