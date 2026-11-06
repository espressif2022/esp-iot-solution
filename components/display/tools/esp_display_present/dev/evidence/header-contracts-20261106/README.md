# 私有头文件职责拆分 · 2026-11-06

基于35f595573。将core/present_presenter.h按配置、基础类型、原生API、owner桥接、非owner控制与会话交接拆分为六个头文件。保留43个函数声明，不新增运行时入口。生产C文件仅调整include。

memory头不再引入engine或backend定义，只前置声明配置结构；engine配置增加struct tag供其引用。memory补齐C++的C linkage，并由已有C++ smoke实际调用C实现验证链接。mode不再引入公共运行时API，plan头不再引入原生engine API；plan C实现为原create_planned桥接显式引用engine头。completion_tracker只引用所需状态/backend类型，output_caps只依赖标准类型。

## 验证

- Host ASan/UBSan与LVGL9.6.0~1：45/45通过，含原有跨任务交接、取消、lease、完成和停止测试。
- 新header_contracts分别独立编译11个头文件的C11/C++17入口，并检查规划、预算、基础类型、session、completion等头文件的依赖边界。临时在memory注入engine API引用后，测试按预期失败；源文件随后恢复，日志保存为negative-contract.log.gz。
- 保留43个原core函数声明，签名逐项对照；所有生产C实现去掉include行后与基线完全一致。
- 用Host、Xtensa和RISC-V编译器分别生成拆分前后类型探针对象，各45项结构尺寸、对齐、字段偏移及枚举值完全一致。具体数值与编译器版本见abi.json；这是所列私有类型的对照，不宣称整套库的二进制ABI兼容性。
- S3 RGB/P4 DSI benchmark包含NONE、DOUBLE_DIRECT与0°/90°，S31 QSPI包含TE_C2_T2与0°/90°，另编译S3 pixels工程。构建manifest与本批源码hash逐项核对。
- S3 pixels最终ELF抽查6个engine、lease桥接、tracker与FB热路径符号，仍在.iram0.text。公共头文件及IDF tracked文件未修改；未烧录，无性能或目视结论。

接口职责与剩余耦合边界见[SOURCE_LAYOUT.md](../../SOURCE_LAYOUT.md)。backend配置仍按值嵌入engine配置，本批没有拆开backend类型与操作接口，也没有改变owner校验、交接提交点或硬件资源退休条件。
