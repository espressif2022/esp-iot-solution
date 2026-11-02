# V1 void 取消语义修正（2026-11-02）

旧门面只有 native cancel 成功才撤销 active/lease；异步访问未结束导致超时后，void 调用没有反馈，下一次 begin 得到 INVALID_STATE。该行为不符合旧调用的逻辑取消契约。

现在取消先撤销门面的 active、lease_id 与 lease，要求下帧完整 raster；内部保留 cancel_pending。零预算尝试 native cancel，不等待整段准入超时。native 因 gate 竞争或真实拷贝未完成而超时，均保留缓冲所有权；取消不创建或提交新帧，也不清 sticky fault。

下次合法 begin 使用同一个准入预算排空取消，再 prepare/begin；超时返回 TIMEOUT，不创建新帧、不毒化实例。quiesce/rebind/LVGL bind/rotation 同样先处理逻辑已撤销的取消；stop 使用现有安全排空，超时保留资源重试。私有 task-neutral bridge 仅供门面已在 session gate 下撤销 producer frame/lease 的场景，不能用于取消仍在绘制的 producer。原生公开取消仍要求 owner，默认 timeout 不改，公共旧头文件不改。

条件变量固定异步 copy 执行顺序，覆盖旧 lease 的 submit/resolve 失效、下一次 begin 有界超时、故障仍为 NONE、取消期间 DMA 源保持原像素；放行后直接 begin 重试、独立控制任务 quiesce 后交接、取消后 delete 超时及释放后重试。没有伪造 copy done 或提前释放缓冲。

该修复只关闭取消这一项，其他旧成功路径缺口见 [兼容缺口清单](../../V1_COMPATIBILITY_GAPS.md)。本轮无烧录，不等于完整 V1 兼容或实板显示验收完成。

追加 GRAM 已接受物理传输的回归：反复取消不新增提交，完成回调前真实 DMA 源仍可读，begin 返回 TIMEOUT，收到真实回调后可直接建帧；持有但未提交 lease 的取消不发送像素。

验收：Host/LVGL 34/34；ASan/UBSan/LSan 34/34（detect_leaks=1）；presenter/scheduler/legacy_resources/legacy_facade 各 50 次，共 200 次通过；S3/P4 离线构建及格式/补丁检查通过。配置、日志与源码 SHA256 在本地忽略目录 artifacts/20261102-cancel-compat。未烧录、未 commit。
