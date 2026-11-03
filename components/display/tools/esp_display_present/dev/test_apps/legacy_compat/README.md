# 原工程兼容层离线构建

此工程只更换组件选择，不修改原示例的 C/C++ 业务源码。使用本地已缓存依赖，关闭组件管理器，禁止自动下载；生成业务源码和 GSP 预编译库 SHA256 清单。不会连接或烧录设备。

```sh
python3 prepare.py --example gsp_lvgl_present_handoff --output /tmp/pv2-legacy-handoff
. /path/to/esp-idf/export.sh
idf.py -C /tmp/pv2-legacy-handoff -B /tmp/pv2-legacy-handoff/build-p4 \
  -DIDF_TARGET=esp32p4 -DSDKCONFIG=/tmp/pv2-legacy-handoff/sdkconfig.p4 \
  '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4' build
```

还支持 `lvgl_present_benchmark`、`gsp_showcase`。输出目录必须不存在。生成的 CMake 显式加入 presenter/LVGL 两个组件、原缓存组件和 hw_init；GSP 的构建依赖原来由 manifest 提供，离线时在 CMake 恢复顺序。原始工程、缓存库和 IDF 不修改。

离线链接通过不等于运行兼容。RGB 必须先确认实际 bounce 配置再打开 `CONFIG_PV2_LEGACY_RGB_BOUNCE_CONFIRMED`；TE、单 FB 等未接通配置仍明确拒绝。板测前必须重新识别设备并选择实际屏幕/引脚/PSRAM 配置，不得直接烧录离线示例配置。

离线模式不运行版本求解器。使用的 GSP package manifest 和各架构 archive SHA256 写入清单；需与原示例声明范围核对。特别是 showcase 原声明为 `1.5.*`，若只缓存 `1.9.0`，其结果只能说明该缓存版本下的源码接入，不是原 1.5 发布组合的兼容证明。
