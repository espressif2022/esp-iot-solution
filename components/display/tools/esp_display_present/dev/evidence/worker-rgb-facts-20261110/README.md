# Worker 命名与 RGB 私有事实解析收敛（2026-11-10）

- 后台状态推进任务命名 backend_worker / present_backend，像素任务 pixel_worker / present_pixel。任务入口、句柄、生命周期和相关注释一起调整；GRAM deferred_submit 仍只允许后台任务进入驱动提交。
- 撤回公开 target.hw.rgb.scanout；公开配置头恢复到 8b25ea151。没有新增公开替代项。
- scanout 内部根据驱动查询或私有 BSP 初始化事实选择协议。驱动查询优先；已知无效事实拒绝，不用旧 bounce 提示覆盖。无查询时 BSP 只报告真实配置，不选择协议。当前请求式切链能力只放行已核对的 S31 AXI 路径；S3 非 bounce 不作为多 FB 退休保证。
- 干净 IDF 的任意借用 panel handle 无法凭空查询创建参数；缺少驱动/BSP 事实时仅保留既有已确认 bounce 回退。测试 BSP 绑定仅用于维护的单面板工程，按 handle 和生命周期匹配。不使用全局 LCD 拦截、IDF 私有结构或 IDF 补丁。

## 验证

Host ASan/UBSan 44项通过；含 LVGL 9.6.0 绑定的48项通过。S3、S31 原生编译及 P4 DSI smoke 编译通过。

S31 Korvo RGB 800×480（无 bounce）与 S3 R16 RGB 480×480（10行 bounce）各96项通过：六种防撕裂 FB 模式 × 四角度 × 生产者 core0/core1 × 驱动返回延迟0/50ms。每项四帧，全量检查物理像素和边界，逐项关闭重建 presenter。每项两个 worker 均在任务内部验证 FreeRTOS 真实名称，原始日志 worker_names=2。192项共768帧；不含 NONE，不是 LVGL FPS benchmark，也没有目视防撕裂验收。

两块板均已恢复原始整片固件并独立校验，见各板 backup-restored.json 和 backup-restore-verify.log。备份镜像保存在客户工作区 hardware-backups/present-worker-converge-20261110，不放入组件目录。干净 IDF rev 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe。

归档前核对两板 manifest 中全部组件源码与测试输入 SHA256 与当前文件一致；本目录不含 build 或固件镜像。后续命名整理不改变本次测试时的 manifest。
