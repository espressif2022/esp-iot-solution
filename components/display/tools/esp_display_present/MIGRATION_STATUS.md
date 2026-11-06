# 组件结构与验证入口

生产组件为 `esp_display_present` 与 `esp_lv_present`。前者包含应用入口、平台装配和唯一核心；后者将 LVGL 接入同一核心。常规 `esp_display_presenter_*` 调用保持，但旧公开几何头、坐标和尺寸类型已收为内部，依赖它们的旧代码会编译失败。内部头不作为应用接口。旧 V2 目录保留历史设计、证据与未迁移的生成物，不再是生产组件。

在本组件目录中，使用已有本地 LVGL 源执行：

```sh
python3 dev/tools/dev.py test --lvgl-dir /path/to/lvgl
ASAN_OPTIONS=detect_leaks=1 python3 dev/tools/dev.py test --lvgl-dir /path/to/lvgl --sanitize
```

`dev/test_apps/smoke` 是离线 IDF 编译入口；`dev/test_apps/legacy_compat/prepare.py` 从原示例生成隔离工程，可用 `--gsp-package` 指定缓存 GSP 1.5.1。构建使用本地 SDK 和依赖，不自动下载。各历史记录的测试结果属于当时版本，不能替代新候选验证。

当前提交的 Host 项、三板 benchmark 及证据见 [当前验证记录](dev/VALIDATION.md)；[历史验证记录](dev/RESULTS.md) 对应更早源码。尚未发布新组件包，真实光学 TE 时序和外部应用兼容性需独立验证。

历史 [三板模式／旋转矩阵](dev/MODE_ROTATION_RESULTS_20261103.md)发现 P4 非方形面板通过公开接口运行时跨轴旋转会提交超出物理尺寸的区域。2026-11-06 已在 Host 复现并修复逻辑／物理尺寸权威冲突，固定物理 panel 的 RGB/DSI 全模式旋转验证通过，仍待 P4 上板复测，见 [架构检查](dev/ARCHITECTURE_AUDIT_20261106.md)。S31 GRAM 以同步原地字节序转换启动时，运行时改到非零旋转仍可返回不支持。

为兼容既有配置，本轮保留 `PV2_LEGACY_*` Kconfig 键。RGB bounce 人工确认仅用于缺少驱动能力查询的旧驱动；可查询时以驱动事实为准。

## 更早的 2026-11-03 交付候选

三板候选日志及逐板恢复校验见 [板测记录](dev/BOARD_ACCEPTANCE_RESULTS.md)，用户已确认三块板测试画面以及 P4 GSP／LVGL 交接界面正常。旧 LVGL 9.0–9.4 已恢复复制回退并完成 Host 验证；组件隔离打包通过，Registry 版本与依赖范围仍需发布前处理，见 [打包结果](dev/PACKAGING_RESULTS.md)。
