# 租约描述收敛与状态归属审计 · 2026-11-06

基于 `0b46be813`，范围见[逐字段审计](../../STATE_OWNERSHIP_20261106.md)。没有把公共active/cancel_pending、成功时间线或producer会话字段当作engine状态的简单副本删除。

本批删除公共层完整lease布局/容量副本，仅保留身份映射。两个私有桥接方法沿用owner检查、engine gate和原valid_lease/resolve/submit实现；不新增资源获取，不省略64位token及指针验证。Acquire返回的公开结构与ID规则不变；提交失败仍可重试，成功或公共cancel才清除映射。

## 验证

- Host ASan/UBSan与LVGL9.6.0~1：44/44通过。新增lease_identity测试；现有公共facade集成测试增加错误优先级、同址复用和取消后失效场景。
- 身份测试覆盖：64位原生ID仅高位不同也拒绝、异任务调用拒绝、错误指针拒绝、坏参数保留原错误优先级、失败后可继续使用原租约、相同地址的新租约不接受旧ID、取消后拒绝旧操作。
- 将保存的基线facade对象替换进同一套新公共集成测试，链接本批engine与相同fixture/LVGL，其余配置不变；同样通过。该对照针对facade表示变化，不代表整套基线固件上板A/B。
- 通过对象文件DWARF读取结构体尺寸，Host基线432、本批384字节。真实目标对象尺寸见object-sizes.json；是结构体大小，不含分配器开销。
- 三芯片完整benchmark编译记录见validation.json及各板manifest/build.log.gz。S3/P4包含NONE/DOUBLE_DIRECT与0°/90°，S31包含TE_C2_T2与0°/90°。仅编译，未烧录。

公开头文件、扫描同步、完成水位和缓冲释放条件未改，IDF tracked文件保持干净。

补充S3纯像素工程编译通过，最终ELF中两个桥接函数均在.iram0.text。LVGL benchmark会裁剪未使用的公共桥接，不能用其符号缺席推断放置；本次以pixels工程验证。三芯片公共对象尺寸均由328降到280字节。
