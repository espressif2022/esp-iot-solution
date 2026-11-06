# ESP Display Present

`esp_display_present` 提供稳定的 `esp_display_presenter_*` 应用接口、平台资源装配和唯一显示引擎；`esp_lv_present` 将 LVGL 接入同一引擎。应用继续持有 panel、IO 和总线资源。

公开应用头位于 `include`；`src` 内头文件均为内部实现，不作为应用 API 导出。当前版本不再公开旧几何头和其中的坐标／尺寸类型；依赖这些接口的应用需单独迁移，不能将其视为完全源码兼容。

本地依赖只需 `esp_display_present`，LVGL 场景再加入 `esp_lv_present`，不再需要 V2 组件。源码合并不代表已发布 Registry 包或确认全部外部兼容。开发和验证入口见 [迁移状态](MIGRATION_STATUS.md)，历史设计见 [交接设计](../esp_display_present_v2/TRANSITION_DESIGN.md)。

ESP32-S3 默认启用 `CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM`，将 FB 引擎及像素拷贝热路径放入内部 RAM。RGB bounce、16 KiB 指令缓存的实测中，这使双 FB PARTIAL／DIRECT 吞吐恢复到约 60 次提交／秒；代价是该构建额外约 19 KiB IRAM。内部内存紧张的应用可关闭此项，关闭后需重新评估 FB 吞吐。放置策略不改变缓冲退休条件，也不代表任务 API 可在 ISR 或关闭外部缓存时调用。测试条件与限制见 [S3 热路径放置复测](dev/FB_IRAM_RESULTS_20261103.md)。

RGB bounce / MIPI DSI 的多缓冲 scanout 在提交成功后先排空已经执行中的 ISR，再以一次后续 `frame_buf_complete` 确认旧 framebuffer 的读取结束；排空不支持或失败时保守等待两次。提交和排空期间的旧回调不归还缓冲，raw VSYNC 不作退休依据。bounce/FIFO 中已经拷出的旧像素继续输出，不需要额外占用旧 framebuffer 一个扫描周期。此路径要求独占 panel、连续扫描和完整的 driver-owned framebuffer；单缓冲 active-write 不保证无撕裂。修复后已完成 S3 RGB 的 7 模式 × 4 角度 LVGL benchmark，28/28 组正常结束，结果及限制见 [2026-11-05 上板矩阵](dev/evidence/rgb-scanout-benchmark-20261105/README.md)。用户在 NONE 阶段报告过撕裂；双/三缓冲仍需完整目视验收，串口完成不代表光学无撕裂。

同条件的 [原始 adapter 对照](dev/evidence/rgb-adapter-compare-20261105/README.md) 已完成：FULL/PARTIAL 基本持平或略高，优化前 NONE/DIRECT 有明显退化。随后已修复 LVGL 已解析像素 DIRECT 的重复预填：按实际区域更新物理图像并做历史 repair，四角度从 21/13/13/13 提升至 39/30/32/31 FPS，见 [DIRECT 修复与复测](dev/evidence/direct-resolved-20261105/README.md)。普通读改写 DIRECT 保留继承语义；NONE 单 FB 完成门控另待处理。所有数据是单轮 LVGL FPS，不是无撕裂证明。

普通内部 tile 的 GRAM 可试用 `CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND`，默认关闭。它复用引擎 sender，将可能阻塞的驱动提交移出引擎 gate，不强制增加像素复制；旋转／PSRAM 路径继续采用原有调度。实验 sender 优先级由 `CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY` 设置，默认 7。适用条件、RAM 成本和实测收益见 [GRAM 后台提交首版](dev/GRAM_BACKGROUND_RESULTS_20261104.md)。

2026-11-06 的 [架构检查与 Host 收敛](dev/ARCHITECTURE_AUDIT_20261106.md)修复了公开接口运行时跨轴旋转的尺寸权威冲突：panel 物理尺寸固定，由 backend 更新逻辑宽高，资源层不再修改 scanout 物理视图。旋转后需重新查询 caps。新增了创建失败回滚和固定物理屏幕的 RGB/DSI/GRAM 旋转验证；该修复尚未重新上板。

当前提交的 [三板 LVGL 帧率、Host 测试项与证据](dev/VALIDATION.md) 单独列出测试条件和边界。[模式与旋转矩阵](dev/MODE_ROTATION_RESULTS_20261103.md)记录了 P4 公开接口运行时跨轴旋转的实板缺陷，以及 S31 GRAM 的运行时旋转限制。TE 的时序假设见 [TE 说明](docs/TE_TIMING.md)。

测试工程、板级配置、LVGL benchmark 与 adapter 对比统一入口：[dev/README.md](dev/README.md)。
