# 生产者交接提交点修复 · 2026-11-06

基于41724c0a3，发现并修复present_session_bind的跨任务失败路径。原顺序core rebind → prepare → drain → 公共身份提交；prepare/drain失败会只改变core owner。新顺序先prepare，再在engine gate内drain并提交owner，最后提交公共身份。

## 验证

- 新增公共集成回归：TE创建失败返回NO_MEM后旧任务仍可开始/取消帧；在途IO未完成导致drain超时，原DMA源仍保留原像素，真实完成后旧任务仍可开始下一帧；跨任务成功bind可调用原生API，成功unbind后旧任务仍需显式rebind。
- 原实现运行新增测试在第一种失败路径复现断言：旧任务无法begin_next_frame。日志before-fix-tests.log.gz记录的是预期回归失败。
- 修复后完整Host ASan/UBSan及LVGL9.6.0~1测试44/44通过；是在既有legacy_facade中扩展用例，CTest数量未增加。
- S3 RGB / P4 DSI的NONE与DOUBLE_DIRECT，S31 QSPI的TE_C2_T2，均包含0°/90°配置，完整benchmark编译通过。manifest保存组件源hash及固件hash，已逐项核对。
- IDF tracked文件保持干净，无公共头文件修改，无烧录、目视或性能验证。

## 边界

失败保持原生产者身份，不表示回滚硬件初始化或已完成receipt。保留资源可供重试，真实传输完成/故障继续由原路径处理。私有control入口要求调用者排除生产者调用，公共gate不负责为外部并发native调用兜底。

本轮不改变成功bind后LVGL对象创建失败的unbind语义；成功交接的身份不会自动还给旧任务。详情见[状态归属说明](../../STATE_OWNERSHIP_20261106.md)。
