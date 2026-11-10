# LCD adapter 命名与平台分组验证（2026-11-10）

基线 e64a21874。LCD 适配器改为 lcd_io、lcd_fb_update、lcd_fb_scanout，集中到 src/platform/lcd；RGB 事实解析随之迁移。TE sync、timing、timer 集中到 src/platform/te。

TE timing 与 timer 不合并：timing 是纯窗口计算，timer 是 IDF / Host 定时器生命周期封装；TE sync 与 TE backend 都使用它们。合并会增加计算层的平台依赖，不能消除重复逻辑。职责及结构图见 [模式架构说明](../../../docs/MODE_ARCHITECTURE.md)。

- 38个相关 C/C++/头文件与基线经明确的内部模块路径/标识符替换、仓库格式化后 token 一致；公开头文件无改动，调度和缓冲行为不变。
- ASan/UBSan + LVGL9.6.0 48项检查全部通过。
- S3 RGB、S31 RGB、P4 DSI、BOX-3 SPI、S31 QSPI 编译链接通过。归档前逐一核对五份 manifest 的组件与测试输入哈希；配置和完整编译日志压缩保存。
- 本轮没有烧录或新增硬件验收，不把前轮上板数据当成本轮记录。历史证据和实验补丁保留当时名称、路径及哈希；当前维护的代码、构建清单、测试和文档链接同步更新。
