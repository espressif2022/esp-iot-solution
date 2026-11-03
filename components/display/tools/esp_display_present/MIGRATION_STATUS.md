# 组件结构与验证入口

生产组件为 `esp_display_present` 与 `esp_lv_present`。前者包含门面、平台装配和唯一核心；后者包含稳定 LVGL 接口与原生 binding。旧应用公开头和调用方式保持，公开扩展与源码兼容范围按命名审计逐项收敛；内部头不作为应用接口。旧 V2 目录保留历史设计、证据与未迁移的生成物，不再是生产组件。

在本组件目录中，使用已有本地 LVGL 源执行：

```sh
python3 dev/tools/dev.py test --lvgl-dir /path/to/lvgl
ASAN_OPTIONS=detect_leaks=1 python3 dev/tools/dev.py test --lvgl-dir /path/to/lvgl --sanitize
```

`dev/test_apps/smoke` 是离线 IDF 编译入口；`dev/test_apps/legacy_compat/prepare.py` 从原示例生成隔离工程，可用 `--gsp-package` 指定缓存 GSP 1.5.1。构建使用本地 SDK 和依赖，不自动下载。各历史记录的测试结果属于当时版本，不能替代新候选验证。

本机构建结果与复现命令见 [验证记录](dev/RESULTS.md)。当前阶段尚未进行迁移后实板验收，也未发布新组件包。设备实际身份、画面、TE 时序以及发布兼容性需独立验证。

为兼容既有配置，本轮保留 `PV2_LEGACY_*` Kconfig 键。RGB bounce 人工确认仅用于缺少驱动能力查询的旧驱动；可查询时以驱动事实为准。

## 2026-11-03 交付候选

三板候选日志及逐板恢复校验见 [板测记录](dev/BOARD_ACCEPTANCE_RESULTS.md)，用户已确认三块板测试画面以及 P4 GSP／LVGL 交接界面正常。旧 LVGL 9.0–9.4 已恢复复制回退并完成 Host 验证；组件隔离打包通过，Registry 版本与依赖范围仍需发布前处理，见 [打包结果](dev/PACKAGING_RESULTS.md)。
