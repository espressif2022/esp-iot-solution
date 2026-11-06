# 私有源码目录整理 · 2026-11-06

基于6115044ca。26个文件从src根目录归入core/config/resources/backend/pixel/platform，保持文件基名、函数名及原有逻辑。src根目录仅保留公共入口和sources.cmake，具体规则见[SOURCE_LAYOUT.md](../../SOURCE_LAYOUT.md)。

## 验证范围

- 对照基线逐文件检查52个生产C/H文件，移除include行后的内容完全一致；维护测试与esp_lv_present中的C/C++变更也仅为include路径。
- 共享显式源码清单解析后包含25个生产C文件，无重复、无遗漏，与基线IDF清单按迁移映射逐项一致。Host原有特殊链接拆分及TE shim保持不变。
- Host ASan/UBSan与LVGL9.6.0~1：44/44通过。
- S3 RGB/P4 DSI benchmark包含NONE、DOUBLE_DIRECT及0°/90°；S31 QSPI benchmark包含TE_C2_T2及0°/90°。另编译S3 pixels工程用于公共API桥接与IRAM核对。
- S3 pixels最终ELF中选取engine acquire、两个公共lease桥接、transfer_finish、completion_reap和FB service共6个保留符号，均在.iram0.text；不要求被链接裁剪的函数出现在ELF中。
- 验证后的组件源hash与各构建manifest一致；IDF tracked文件保持干净。未烧录，无帧率或目视验证。

本轮不拆分core/present_presenter.h，不调整状态机或缓冲交接机制。历史证据保持原路径；维护入口与消费者同步更新。目录归类不代表已有类型依赖全部解除。
