# 组件合并验证记录（2026-11-03）

本文记录的是本轮重构之前的历史源码。当前提交的 Host 测试集合、三板帧率及兼容边界见 [当前验证](VALIDATION.md)；尤其不可将本文的测试数量或公开几何头结论直接套用到当前提交。

这是合并后、backend 私有头拆分和格式整理后的本地结果；均退出码 0。没有烧录、上板或发布组件。日志和构建目录位于 `/tmp/present-component-audit/`，是本机临时证据，不是随组件发布的文件。

使用 ESP-IDF `v6.2-dev-2221-g7b9cc1ac79f8-dirty`、本地 LVGL 9.5.0 和 GSP 1.5.1 缓存。稳定应用的四个 presenter 公开头及一个 LVGL 公开头，迁移前后 SHA256 一致。

| 检查 | 结果 | 日志（临时目录下） |
| --- | --- | --- |
| Host + 真实 LVGL | 37/37 | `final2-host.log` |
| ASan/UBSan，LSan detect_leaks=1 + LVGL | 37/37 | `final2-san.log` |
| 无 LVGL Host | 32/32 | `final2-minimal.log` |
| IDF 离线 smoke S3 / P4 / S31 | 三个构建链接通过 | `final2-idf-0.log` / `final2-idf-1.log` / `final2-idf-2.log` |
| P4 GSP 1.5.1 showcase / LVGL handoff | 两个工程构建链接通过 | `final2-idf-3.log` / `final2-idf-4.log` |
| 旧 API 独立 consumer，仅声明 presenter | S3 Component Manager 本地 path 构建通过 | `legacy-consumer.log`、`final2-legacy-consumer.log` |
| scanout boundary 工程生成 | 最终源码生成成功，未上板 | `/tmp/present-final-boundary-check2/instrumentation_manifest.json` |

两个 GSP link map 引用的 `libesp-gsp.a` 经 symlink 解析至 `~/.cache/Espressif/ComponentManager/service_d92d8f1e/espressif__esp-gsp_1.5.1_595b3b43/prebuilt/esp32p4/libesp-gsp.a`，SHA256 为 `5ab508eda956a05966ddbe16b186a367e2f19e90dc5fbce0cc1b668eb8a78cab`。离线 staging 记录业务源散列；该验证是链接验证，不表示运行、显示效果或时序验收。

复现本次 Host 命令（从仓库根目录，实际 LVGL 路径按本机替换）：

```sh
python3 components/display/tools/esp_display_present/dev/tools/dev.py test --lvgl-dir /home/xuxin/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.5.0_184e5325 --build-dir /tmp/present-component-audit/build-host --jobs 4
ASAN_OPTIONS=detect_leaks=1 python3 components/display/tools/esp_display_present/dev/tools/dev.py test --lvgl-dir /home/xuxin/.cache/Espressif/ComponentManager/service_d92d8f1e/lvgl__lvgl_9.5.0_184e5325 --sanitize --build-dir /tmp/present-component-audit/build-sanitize --jobs 4
python3 components/display/tools/esp_display_present/dev/tools/dev.py test --build-dir /tmp/present-component-audit/build-minimal --jobs 4
```

三个 IDF smoke 由 `dev/test_apps/smoke` 在禁用 Component Manager 条件下分别针对 `esp32s3`、`esp32p4`、`esp32s31` 配置，最终用 `IDF_COMPONENT_MANAGER=0 ninja -C /tmp/present-component-audit/<build> -j 4` 重建；build 依次为 `idf-s3`、`idf-esp32p4`、`idf-esp32s31`。GSP 通过 `dev/test_apps/legacy_compat/prepare.py --example <gsp_showcase|gsp_lvgl_present_handoff> --gsp-package <本地1.5.1目录> --output <新目录>` 生成，以 P4 和本地 SDK 构建，最后用相同 ninja 方式增量重建。独立 consumer 在 `/tmp` 保留其原源码与仅有 `esp_display_present` 的 manifest，将 path 指向本地新组件，启用 Component Manager 编译。

一次初始的重生成未传 `IDF_COMPONENT_MANAGER=0`，因 sandbox 禁止向原 smoke 目录写 `dependencies.lock` 而失败；显式设为 0 后全部通过。`dev.py idf-build` 已在调用时设置该变量，避免联网求解及写入源目录。

源码迁移映射见 [MIGRATION_FILES.json](MIGRATION_FILES.json)。结果不推断 Registry 包、外部扩展 ABI 或产品实板兼容。

## LVGL 9.0–9.5 兼容恢复（2026-11-03）

以下是恢复 LVGL 9.0–9.4 兼容后，使用对应真实 LVGL 源码的 Host 验证。旧 API 在 9.0–9.4 使用独立绘制缓冲和拷贝回退；原生 pooled / FULL zero-copy 入口在这些版本明确返回 `ESP_ERR_NOT_SUPPORTED`。9.5 可在满足准入条件时使用池化路径。测试结果不表示覆盖所有 LVGL 次版本、所有绘制场景或实板时序。

| LVGL | 全套 Host | 旧 API FB 补测 | ASan / UBSan |
| --- | --- | --- | --- |
| 9.0 | 37/37 | `legacy_facade` 1/1 | 全套 37/37；旧 API FB 补测 1/1 |
| 9.1 | 37/37 | `legacy_facade` 1/1 | 未做该版本专项 |
| 9.2 | 37/37 | 包含在全套 `legacy_facade` | 未做该版本专项 |
| 9.3 | 37/37 | 包含在全套 `legacy_facade` | 未做该版本专项 |
| 9.4 | 37/37 | `legacy_facade` 1/1 | 未做该版本专项 |
| 9.5 | 37/37 | `legacy_facade` 1/1 | 全套 37/37；旧 API FB 补测 1/1 |

全套日志为 `/tmp/lvgl90-all.log`、`/tmp/lvgl91-all.log`、`/tmp/lvgl92-all.log`、`/tmp/lvgl93-all.log`、`/tmp/lvgl94-all.log`、`/tmp/lvgl95-all.log`；9.0 / 9.5 全套 sanitizer 日志分别为 `/tmp/lvgl90san-all.log` 和 `/tmp/lvgl95san-all.log`。旧 API FB 补测发生在部分全套运行之后，其日志为 `/tmp/legacy-fb-{90,91,94,95}-test.log`，对应 sanitizer 补测为 `/tmp/legacy-fb-{90,95}-san.log`；因此不能将先前全套执行描述为包含后来新增的 FB 用例。新增用例通过旧绑定检查 DOUBLE FULL / DIRECT 下的局部更新和显示像素。

两个归档的隔离消费仅在 LVGL 9.5.0 与 S3 离线构建下验证，见 [PACKAGING_RESULTS.md](PACKAGING_RESULTS.md)；板上运行与时序不由此推断。现有 presenter 1.2.0 / LVGL binding 0.1.2 与 Registry 缓存旧版本冲突，且现有 presenter 依赖范围允许解析旧实现，正式发布前仍需确定新版本和依赖下限并验证 Registry 解析。
