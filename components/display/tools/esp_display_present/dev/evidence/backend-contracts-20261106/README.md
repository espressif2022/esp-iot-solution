# Backend配置与操作表分离 · 2026-11-06

基于bbdb3f3bb。原backend/present_backend.h拆为present_backend_types.h、present_backend_config.h、present_backend.h：协议类型共享；完整配置和操作表分别依赖协议类型，互不导出对方。配置指针用原有不透明声明；不改结构字段、操作表顺序、函数指针或三个backend实例。

control、producer、LCD适配、transfer tracker、frame core、records及transaction改用协议类型。completion tracker真实调用ops，保留操作表依赖。构造或读取完整配置的维护代码显式包含config头。生产C实现仅调整include。

## 验证

- 原类型、配置和操作表三组声明文本逐字保留，sha256记录在abi.json；所有生产C文件移除include行后与基线完全一致。
- Host ASan/UBSan与LVGL9.6.0~1：45/45通过，包括原交接、lease、取消、停止、扫描及TE测试。
- header_contracts扩展至17个头文件的C11/C++17独立编译；检查配置与操作表互不引入、类型头不引入配置/操作表/TE timing，以及上层规划和平台接口不带出操作表。临时在config注入dispatch include后按预期失败，随后恢复源码；negative-contract.log.gz保存负向验证日志。
- Host、Xtensa、RISC-V各16种结构类型、147项尺寸/对齐/字段偏移/枚举值对照完全一致，包括backend配置、panel adapter、TE gate和ops表。具体数值与编译器版本见abi.json；不代表整套组件的二进制兼容性声明。
- S3 RGB/P4 DSI benchmark含NONE、DOUBLE_DIRECT及0°/90°；S31 QSPI含TE_C2_T2及0°/90°。另编译S3 pixels工程并抽查最终ELF中6个热路径符号的IRAM放置。manifest源hash与最终文件逐项核对。
- IDF tracked文件和公共头文件保持干净。未烧录，没有帧率、功耗或目视结论。

本轮只收敛backend头文件依赖，不改扫描/退休条件或缓冲交接。协议类型仍引用pixel头中的布局/surface类型，未继续拆pixel接口。当前职责与依赖图见[SOURCE_LAYOUT.md](../../SOURCE_LAYOUT.md)。
