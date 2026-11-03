# 同配置 V1/V2 实板对照

先激活对应 ESP-IDF 环境，提供本地 LVGL 9.5 源码：

```sh
python3 test_apps/legacy_compat/prepare_board.py --target esp32p4 \
  --output /tmp/present-probe-p4 --lvgl-dir /path/to/lvgl
idf.py -C /tmp/present-probe-p4 -DIDF_TARGET=esp32p4 -DPROBE_IMPL=v2 build
idf.py -C /tmp/present-probe-p4 -DIDF_TARGET=esp32p4 -DPROBE_IMPL=v1 build
```

S3 使用 esp32s3，S31 使用 esp32s31。默认包含模式/取消后重绘测试和官方 benchmark；S31 另测 TE 单/双 compose。`--skip-te` 明确排除 TE 用例，只用于独立 GRAM 对照，不代表 TE 通过。工具不连接设备、不下载依赖、不烧录。

两实现共享业务源码、板级初始化、LVGL 配置、任务优先级和 tile 尺寸；切换仅改变组件选择与日志标签。每次构建后先保存固件、配置及 SHA，再构建另一实现。烧录前重新查询芯片/MAC；不要把历史端口号当板子身份。测试结束恢复并校验备份。

完整 summary 必须含有效场景；SCENE 数值按 measurement_cnt 归一化。早期证据日志中的逐场景 SCENE 是累加器，不能当平均值；BENCH_DONE 已用 LVGL 汇总值正确归一化。测试通过还必须检查 watchdog、错误、实际画面；ALL_PASS 标签本身不足以完成稳定性或视觉验收。

早期循环在零延时时 taskYIELD，S31 出现 IDLE0 watchdog 告警。当前改为至少阻塞 1 tick，让 idle 任务获得运行机会；对两实现使用同一循环，不放宽 watchdog。旧数据与新循环的数据必须分轮报告。
