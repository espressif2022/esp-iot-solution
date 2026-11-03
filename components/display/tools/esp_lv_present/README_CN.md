# ESP LV Present

`esp_lv_present` 提供 LVGL 9 稳定接口和原生 binding，使用 `esp_display_present` 中的唯一引擎。现有 `esp_lv_present_*` 应用接口保持可用。扩展头位于 `include/advanced`，其长期边界仍在审定。

本地依赖 `esp_lv_present`、`esp_display_present` 和 LVGL 9.0 及以上的 9.x 版本。LVGL 9.0–9.4 使用独立绘制缓冲并拷入 presenter；原生借池及 FULL 零拷贝创建会返回 `ESP_ERR_NOT_SUPPORTED`。开发和验证入口见 [迁移状态](../esp_display_present/MIGRATION_STATUS.md)。本次源码迁移不代表 Registry 发布。

使用 Component Manager 消费这份本地源码时，请在应用的 manifest 中**同时明确指定两个本地组件路径**。目前的 presenter 版本约束也会匹配 Registry 中缺少新内部桥的旧包，不能直接按现有 manifest 发布。已验证的本地接入方式和待处理版本问题记录在源码仓库的 `esp_display_present/dev/PACKAGING_RESULTS.md`。
