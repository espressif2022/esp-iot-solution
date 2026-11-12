# ESP LV Present

`esp_lv_present` 提供 LVGL 9 稳定接口，使用 `esp_display_present` 中的显示引擎。公开应用头位于 `include`；`src` 内头文件为内部实现。

本地依赖 `esp_lv_present`、`esp_display_present` 和 LVGL 9.0 及以上的 9.x 版本。LVGL 9.0–9.4 使用独立绘制缓冲并拷入 presenter；原生借池及 FULL 零拷贝创建会返回 `ESP_ERR_NOT_SUPPORTED`。开发和验证入口见 [迁移状态](../esp_display_present/MIGRATION_STATUS.md)。本次源码迁移不代表 Registry 发布。

消费这份尚未发布的候选源码时，请在应用 manifest 中**同时明确指定两个本地组件路径**，仓库内示例已这样配置。绑定要求 present `^2.0.0`，有意排除 1.x。仅依赖 Registry 的接入须先发布兼容的 present 2.0.0，再发布本绑定；版本声明不代表候选包已在 Registry 可用。见[兼容与迁移说明](../esp_display_present/COMPATIBILITY.md)及[验证摘要](../esp_display_present/dev/VALIDATION.md)。
