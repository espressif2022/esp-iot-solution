> Historical measurements: the FB_IRAM Kconfig option and whole-object linker mapping were removed on 2026-11-10. These results describe the older configuration, not the current default.

# S3 双 FB 剩余吞吐修复：热路径内部 RAM 放置

## 结果

剩余的 S3 双 PARTIAL／DIRECT 吞吐差距已在本次指定合成负载下修复。最终组件新增链接放置策略，没有新增退休状态、通知协议、拷贝线程或 IDF 驱动接口。

最终 A/B 使用未修改的 IDF `esp_lcd`：同板、同一生产者源码、480×480 RGB bounce、RGB565、0°、16 行双 tile、生产者优先级 6、1 kHz tick、16 KiB 指令缓存；每项预热 8 帧，测量 60 帧并 drain，重复三次。数值为三次提交吞吐的算术平均，单位次／秒。

| 模式 | 同驱动关闭 FB_IRAM | 最终默认开启 FB_IRAM |
| --- | ---: | ---: |
| 双 PARTIAL | 30.29 | **60.13** |
| 双 DIRECT | 58.28 | **60.11** |
| 三 PARTIAL | 50.84 | **60.12** |
| 三 FULL | 59.78 | **60.11** |

原始 V1 同负载的双 PARTIAL／DIRECT 为 60.11／60.08。前一阶段 ISR join 版本对应 30.29／56.12；那组采用只读查询 overlay，不能把其 DIRECT 数值与本次原版驱动关闭选项的 58.28 混为同一次 A/B。历史条件见 [ISR join 复测](FB_ISR_JOIN_RESULTS_20261103.md)。

这是合成生产者吞吐，不是 LVGL benchmark 或光学帧率。最终双 PARTIAL／DIRECT 的最大单帧调用耗时仍分别为 22,468／21,910 μs，不能据平均吞吐宣称逐帧硬实时。

## 症结与证据

两次回调导致的额外退休周期，已由前一阶段提交门控／ISR join 处理。本次没有降低退休条件。

剩余差距与代码及只读数据放置有关：只把当前组件整体放入内部 RAM、保持调用和任务逻辑不变，双 PARTIAL 即从约 30 恢复到约 60；缩到引擎与 FB 两个单元后约为 58.51；补齐像素执行热路径后恢复到约 60.11。最终以原版驱动复测得到上表结果。

同驱动 A/B 中，双 PARTIAL 在 begin 返回后，调用方 caps／acquire／rows／render／submit／commit 的累计计时从平均每帧 **17.08 ms 降至 13.37 ms**。前者超过约 16.6 ms 的扫描周期，后者具备赶上下一边界的余量。这些计时包含等待和被抢占时间，不是 CPU 净执行时间。

这支持外部 Flash 热路径访问、缓存与 PSRAM 扫描竞争是关键因素；本轮未采集 cache-miss 或总线硬件计数器，不能进一步量化每种访问的贡献。`noflash` 同时改变代码和相关只读数据的放置，不能将收益全部归为某一条 memcpy 指令。

CPU 同步／异步、逐行拷贝、tile 写回、通知裁剪、执行器短路和 GDMA 等早期候选均未单独恢复吞吐，未合入。阶段记录见 [早期排查](S3_REMAINING_INVESTIGATION_20261103.md)。

## 最终改动与内存代价

- [Kconfig](../Kconfig) 新增 `CONFIG_ESP_DISPLAY_PRESENT_FB_IRAM`，只在 ESP32-S3 可选，默认开启；内存紧张的应用可关闭。
- [linker.lf](../linker.lf) 放置 engine、FB backend、pixel engine、执行器分派及 CPU 执行器，并只选择 resources 中的 `copy_execute`，不把整个资源创建单元搬入 RAM。
- [CMakeLists.txt](../CMakeLists.txt) 注册链接片段。没有修改本轮之前的 C 运行时逻辑；前一阶段归档的五个关键运行时文件 SHA-256 均保持一致。

原版驱动 A/B 的链接 map 差额：

| 区域 | 开启相对关闭的增量 |
| --- | ---: |
| IRAM text | **19,236 B（18.79 KiB）** |
| DRAM data | 316 B |
| DRAM bss | 8 B |

这是当前构建的段大小差额；实际堆容量还受链接对齐影响。放置策略不使任务 API 具备 ISR 或关闭外部缓存时的调用能力。P4 配置不启用此项，本轮 P4 构建已通过，没有重测 P4／S31 的实板吞吐。

## 驱动与所有权验证

最终性能 A/B 直接使用 IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 工作区中的 `components/esp_lcd`，该目录 git diff 为空，没有只读查询 overlay 或退休驱动补丁。测试 BSP 明确设置 `bounce_buffer_size_px = WIDTH * 10`，因此对无查询接口的旧驱动使用既有 `CONFIG_PV2_LEGACY_RGB_BOUNCE_CONFIRMED=y` 声明。

独立插桩运行双 PARTIAL／DIRECT，各预热 4 帧、测量 30 帧；包含创建期 bootstrap，共 **70 次扫描确认、68 次旧 FB 释放**。每次确认均为一个已布防的完成回调；未记录提前释放、向仍在读取的 FB 授予写权限、driver restart、underrun 或记录丢失。旧 FB 末次读取到释放的最小记录间隔分别为 31／29 μs。

插桩使用隔离的诊断驱动／组件副本，不能拿其吞吐替代上述未插桩性能结果；这仍是有限场景覆盖，不是穷尽全部压力、reset 和并发条件的安全证明。此前插桩遇到的 restart 历史记录保留，本轮未复现不等于已证明其所有诱因消失。

Host／LVGL **36/36** 通过。所有本轮板卡运行结束后均恢复原始内容，并独立校验完整 16 MiB flash 与备份一致。

## 原始证据

[证据目录](evidence/fb-iram-20261103/README.md) 包含原始性能日志、逐次统计、完整 map 压缩文件、关键符号地址、固件和源码快照、Host 日志、诊断轨迹及解析、设备恢复校验与 SHA-256 清单。
