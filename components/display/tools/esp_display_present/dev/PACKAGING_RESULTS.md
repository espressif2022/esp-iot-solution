# 组件打包及隔离消费验证（2026-11-03）

本次是本地候选源码验证，不是 Registry 发布或 Registry 下载验证。两个包的版本仍分别为 `1.2.0` 和 `0.1.2`；发布前必须解决下文的版本冲突。本次没有上板或提交。

## 打包与结果

使用 ESP-IDF `v6.2-dev-2221-g7b9cc1ac79f8-dirty` 的 Component Manager `compote component pack`，对当前两个源码组件分别打包；命令同时生成单独的 examples 归档。以下哈希仅标识这次测试的本地归档，不代表已发布制品：

| 组件归档 | SHA-256 |
| --- | --- |
| `esp_display_present_1.2.0.tgz` | `522d3f0727b0e1c6e839cb64faa64ab2824f0515cb2aab732077d5f2f0f8d7c9` |
| `esp_lv_present_0.1.2.tgz` | `fdb57f98bc0d4d39f47e02881069170d7612a6ac4922760a20642a5f67d7a3bf` |

Presenter 归档排除 `dev`，包含引擎源码和 `src/legacy_internal.h`。LVGL 组件经 CMake 从 presenter 组件目录取得该私有桥；两个组件因此需要作为匹配版本交付。

| 场景 | 结果 | 限制 |
| --- | --- | --- |
| 只使用解压后的 presenter 包，引用稳定 API 和扩展 API | S3 构建并链接通过 | Component Manager 关闭，组件目录显式指定 |
| 两个归档解压在不同父目录，应用通过 Component Manager 顶层 `path` 声明两个包及本地 LVGL 9.5.0 | S3 配置、构建并链接通过 | 本地 path 解析，不代表 Registry 下载解析 |
| 构建路径审计 | 26 个 presenter/LVGL 源文件的编译命令无原工作树或 `_v2` 路径 | SDK 和 LVGL 使用本地副本；不验证运行时显示 |

两包 consumer 的 `legacy_lvgl.c` 编译命令中的 presenter 私有 include 指向解压包，而非工作树。最终日志在 `/tmp/present-packaging/final-config.log`、`reviewed-build.log`、`reviewed-presenter-build.log`；临时目录可能被系统清理。

## 依赖解析修正与 LVGL 边界

原 `esp_lv_present/idf_component.yml` 的 `override_path: "../esp_display_present"` 在一个独立目录仅解压 LVGL 包时，若顶层未覆盖 presenter，Component Manager 会因该相邻路径不存在而直接失败。已从组件 manifest 删除硬编码的本地 override，应用可按需在顶层声明两个本地组件路径；此修正没有改动任何组件版本。

兼容实现将 LVGL 依赖维持在 `>=9,<10`。LVGL 9.0–9.4 下，旧 `esp_lv_present_start()` 遇到不支持的池化 / FULL 零拷贝入口，会回退到独立绘制缓冲及复制路径；原生 pooled / FULL zero-copy 创建在这些版本返回 `ESP_ERR_NOT_SUPPORTED`。从 9.5 起可使用满足条件的池化路径。**本次实际归档隔离构建只覆盖 LVGL 9.5.0**；9.0–9.4 行为以各版本 Host 回归结果为准，不把此次 S3 打包构建扩大为全版本或实板验证。

## 发布前必须处理的版本问题

本机 Component Manager 缓存已有旧的 `espressif/esp_display_present` 1.2.0 和 `espressif/esp_lv_present` 0.1.2；[Registry 的 presenter 1.2.0 页面](https://components.espressif.com/components/espressif/esp_display_present/versions/1.2.0/changelog?language=en)也证实该版本已发布。在线检索未能证明候选新版本号可用，发布前仍需以 Registry 实时解析结果确认；缓存中旧 presenter 1.2.0 不含新 LVGL 实现需要的 `src/legacy_internal.h`。当前 `esp_lv_present` 对 presenter 的 `^1.1.2` 约束允许选择旧实现。仅删除 override 并不能保证注册表求解选中匹配的新包。

正式发布之前应先核查 Registry 中实际可用版本及其内容，给两个候选包确定未使用的新版本，并将 LVGL 包对 presenter 的版本下限提高到**首次包含此私有桥的新版本**；还要验证 Registry 元数据解析、下载和锁文件。可以考虑 presenter 新增次版本、LVGL 相应升级，但最终版本号以发布策略与 Registry 实际情况为准。本轮没有改版本、上传或验证 Registry 行为。

## 复现本地验证

从激活了目标 IDF Python 环境的 shell 执行（以下目录需自行创建；本地 LVGL 源码不在原组件仓库内）：

```sh
compote component pack \
  --project-dir /home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_display_present \
  --name esp_display_present --dest-dir /tmp/present-packaging/reviewed2-presenter
compote component pack \
  --project-dir /home/xuxin/xuxin/esp-iot-solution-sensor-MR/components/display/tools/esp_lv_present \
  --name esp_lv_present --dest-dir /tmp/present-packaging/reviewed2-lvgl
```

将归档分别解压到互不相邻的独立目录。两包 consumer 的 `main/idf_component.yml` 直接声明三条本地依赖，分别指向解压后的 presenter、LVGL binding 和 LVGL 9.5.0；`main` 依赖 `esp_lv_present`，并实际引用 `esp_display_presenter_create()`、`esp_display_present_engine_create()` 和 `esp_lv_present_start()`，从而要求对应实现参与链接。此轮 consumer 和 lock 保留在 `/tmp/present-packaging/isolated/managed-no-sibling/`。在已激活 IDF 环境下，使用该工程执行：

```sh
idf.py -C /tmp/present-packaging/isolated/managed-no-sibling \
  -B /tmp/present-packaging/isolated/managed-no-sibling/build \
  -DIDF_TARGET=esp32s3 -j 4 reconfigure
ninja -C /tmp/present-packaging/isolated/managed-no-sibling/build -j4
```

Presenter 单包 consumer 位于 `/tmp/present-packaging/isolated/presenter-only/`，只引入 presenter 解压目录。先以 `IDF_COMPONENT_MANAGER=0` 配置 S3 工程，再运行同样环境下的 `ninja -C /tmp/present-packaging/isolated/presenter-only/build -j4`。以上 `/tmp` 工程仅是本机的复现证据，不作为随包交付内容。
