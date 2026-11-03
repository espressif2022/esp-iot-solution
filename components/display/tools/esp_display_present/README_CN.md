# ESP Display Present

`esp_display_present` 提供稳定的 `esp_display_presenter_*` 应用接口、平台资源装配和唯一显示引擎；`esp_lv_present` 将 LVGL 接入同一引擎。应用继续持有 panel、IO 和总线资源。

公开应用头位于 `include`，扩展头位于 `include/advanced`，其长期公开边界仍在审定；`src` 内头文件为内部实现。部分旧 `pv2_*` 扩展通过兼容头提供源码迁移支持，不提供旧二进制符号；实验性的 backend 直接调用不在兼容范围。

本地依赖只需 `esp_display_present`，LVGL 场景再加入 `esp_lv_present`，不再需要 V2 组件。源码合并不代表已发布 Registry 包或确认全部外部兼容。开发和验证入口见 [迁移状态](MIGRATION_STATUS.md)，历史设计见 [交接设计](../esp_display_present_v2/TRANSITION_DESIGN.md)。
