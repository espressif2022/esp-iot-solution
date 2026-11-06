# 架构检查证据 · 2026-11-06

完整结论见 [架构审计](../../ARCHITECTURE_AUDIT_20261106.md)。本目录保留旧实现的固定物理屏幕反例、140 个资源失败点的结果、最终 ASan/UBSan/LSan 37/37 回归、S3/P4 编译和格式检查记录。

日志为 gzip；tested-sources.tar.gz 为本轮改动源码/测试及审计文档，changes.diff 基于 manifest 中的 Git commit。没有 build 目录、设备原始 Flash 或已烧录声明。静态检查和 Host 状态模型不能替代真实 ISR/cache/TE/光学验证。复现使用 dev/tools/dev.py test --sanitize --lvgl-dir <本地 LVGL 9 源> --build-dir <Host 输出>，设置 ASAN_OPTIONS=detect_leaks=1。
