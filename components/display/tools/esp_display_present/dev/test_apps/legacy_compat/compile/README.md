# 独立旧接口消费工程（仅离线编译）

main 只声明 `REQUIRES esp_display_present`，包含旧 presenter 与 geometry 公开头并引用其符号。项目只依赖默认旧组件入口，核心已包含在该组件内；不依赖原 V1 组件、业务示例或 BSP 的间接 include 路径。不要烧录：没有初始化任何板级硬件。

激活本地 ESP-IDF 后，在此目录执行（替换 TARGET 为 esp32p4、esp32s3 或 esp32s31）：

```sh
IDF_COMPONENT_MANAGER=1 idf.py --preview -B /tmp/pv2-legacy-TARGET \
  -DIDF_TARGET=TARGET -DSDKCONFIG=/tmp/pv2-legacy-TARGET/sdkconfig build
```

`--preview` 用于本地 SDK 的 S31 预览目标。全部构建目录、sdkconfig 位于 /tmp；不下载依赖。使用的 SDK 补丁见上级目录相邻的 `../../driver_patches/README.md`。
