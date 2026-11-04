# ESP Display Present

`esp_display_present` 提供稳定的 `esp_display_presenter_*` 应用接口、平台资源装配和唯一显示引擎；`esp_lv_present` 将 LVGL 接入同一引擎。应用继续持有 panel、IO 和总线资源。

公开应用头位于 `include`；`src` 内头文件均为内部实现，不作为应用 API 导出。当前版本不再公开旧几何头和其中的坐标／尺寸类型；依赖这些接口的应用需单独迁移，不能将其视为完全源码兼容。

本地依赖只需 `esp_display_present`，LVGL 场景再加入 `esp_lv_present`，不再需要 V2 组件。源码合并不代表已发布 Registry 包或确认全部外部兼容。开发和验证入口见 [迁移状态](MIGRATION_STATUS.md)，历史设计见 [交接设计](../esp_display_present_v2/TRANSITION_DESIGN.md)。

ESP32-S3 默认启用 `CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM`，将 FB 引擎及像素拷贝热路径放入内部 RAM。RGB bounce、16 KiB 指令缓存的实测中，这使双 FB PARTIAL／DIRECT 吞吐恢复到约 60 次提交／秒；代价是该构建额外约 19 KiB IRAM。内部内存紧张的应用可关闭此项，关闭后需重新评估 FB 吞吐。放置策略不改变缓冲退休条件，也不代表任务 API 可在 ISR 或关闭外部缓存时调用。测试条件与限制见 [S3 热路径放置复测](dev/FB_IRAM_RESULTS_20261103.md)。

普通内部 tile 的 GRAM 可试用 `CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND`，默认关闭。它复用引擎 sender，将可能阻塞的驱动提交移出引擎 gate，不强制增加像素复制；旋转／PSRAM 路径继续采用原有调度。实验 sender 优先级由 `CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY` 设置，默认 7。适用条件、RAM 成本和实测收益见 [GRAM 后台提交首版](dev/GRAM_BACKGROUND_RESULTS_20261104.md)。

当前提交的 [三板 LVGL 帧率、Host 测试项与证据](dev/VALIDATION.md) 单独列出测试条件和边界。[模式与旋转矩阵](dev/MODE_ROTATION_RESULTS_20261103.md)记录了 P4 公开接口运行时跨轴旋转的实板缺陷，以及 S31 GRAM 的运行时旋转限制。TE 的时序假设见 [TE 说明](docs/TE_TIMING.md)。
