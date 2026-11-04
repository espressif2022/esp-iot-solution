# 同配置旧版／当前版实板对照

本工程使用相同的板级初始化、LVGL 9.5 benchmark、任务优先级和绘制配置，分别运行当前生产组件和单独导出的旧版源码。旧版只用于对照，不加入生产组件。准备脚本不下载依赖、不连接设备、不烧录。

先准备本地 LVGL、板级驱动依赖并激活 ESP-IDF。以下命令从仓库根目录执行；输出目录必须不存在：

```sh
LVGL_DIR=/path/to/lvgl
REF_DIR=/tmp/present-v1-reference
mkdir -p "$REF_DIR"
git archive 41b0c9b9b components/display/tools/esp_display_present components/display/tools/esp_lv_present | tar -x -C "$REF_DIR"

python3 components/display/tools/esp_display_present/dev/test_apps/legacy_compat/prepare_board.py \
  --target esp32p4 --output /tmp/present-probe-p4-current --lvgl-dir "$LVGL_DIR"
python3 components/display/tools/esp_display_present/dev/test_apps/legacy_compat/prepare_board.py \
  --target esp32p4 --output /tmp/present-probe-p4-reference --lvgl-dir "$LVGL_DIR" \
  --reference-components "$REF_DIR/components/display/tools"

idf.py --preview -C /tmp/present-probe-p4-current -D IDF_TARGET=esp32p4 build
idf.py --preview -C /tmp/present-probe-p4-reference -D IDF_TARGET=esp32p4 build
```

S3 和 S31 分别使用 `esp32s3`、`esp32s31`。不传 `--reference-components` 固定选择当前组件；传入包含两个旧组件的目录时固定选择旧版。`-DPROBE_IMPL=v1` 不会切换组件源码。

默认先做模式和取消后重绘测试，再运行官方 benchmark。S31 会测试 480×480 整屏 TE 单／双 compose，benchmark 本身使用无 TE 的 GRAM。`--skip-te` 明确排除 TE，不能视作 TE 通过。每次构建应保存固件、配置、源码版本和 SHA；烧录前按芯片与 MAC 重新确认设备，备份、校验，测试后恢复并再次校验。

汇总必须包含 16 个有效场景和 `BENCH_DONE`；逐场景 `SCENE` 已按 `measurement_cnt` 归一化。FPS 和时间为 LVGL 的整数统计，CPU 指标不是逐核占用率。日志还要检查 watchdog、错误及测试环境。仅凭 `ALL_PASS` 或整屏 TE 日志不能证明光学无撕裂。

当前循环最少阻塞一个 tick，让 idle 任务运行。与使用 `taskYIELD` 的旧记录比较时应说明循环差异；同一轮 V1 与当前版应使用相同循环、分辨率和配置。最新结果见 [本轮验证记录](../../../VALIDATION.md)，历史结果见 [2026-11-02 记录](../RESULTS_CLOSE_20261102.md)。

## 模式／旋转矩阵对照

使用同一测试源分别编译原始 V1 和当前组件。输出目录仍须不存在；不要把旧组件放入生产组件目录：

```sh
python3 components/display/tools/esp_display_present/dev/test_apps/legacy_compat/prepare_board.py \
  --target esp32p4 --output /tmp/present-matrix-p4-current --lvgl-dir "$LVGL_DIR" --mode-matrix
python3 components/display/tools/esp_display_present/dev/test_apps/legacy_compat/prepare_board.py \
  --target esp32p4 --output /tmp/present-matrix-p4-v1 --lvgl-dir "$LVGL_DIR" --mode-matrix \
  --reference-components "$REF_DIR/components/display/tools"

idf.py --preview -C /tmp/present-matrix-p4-current -D IDF_TARGET=esp32p4 build
idf.py --preview -C /tmp/present-matrix-p4-v1 -D IDF_TARGET=esp32p4 build
```

其他板对应 `esp32s3`、`esp32s31`，生成器只创建工程，上述构建命令不会连接设备或烧录。烧录前核对芯片／MAC，备份并校验原始 Flash，完成后恢复并独立校验。工具链、板驱动、sdkconfig、测试源码必须一致；旧版不支持的创建组合保留 `CASE_REJECT`，不能记作通过。每个组合 12 帧、每帧等待 `quiesce` 后以独立参考检查像素和提交区域。S31 TE 使用物理整屏，GRAM 使用全宽 band。

`total_frame_us` 仅累加 begin、应用生成图案、提交、commit 和 quiesce 的时间，包含测试包装中 GRAM 像素抓取，不包含后续像素校验及间隔。V1 和当前版本 FB 的 quiesce 等待条件不同，因此本测试适合检查行为和记录耗时，不能把其倒数等同于同一显示完成语义下的吞吐，也不是 LVGL benchmark。一次运行不足以作性能保证。结果及原始日志见 [V1／当前版模式旋转对照](../../../V1_CURRENT_MODE_ROTATION_20261103.md)。
