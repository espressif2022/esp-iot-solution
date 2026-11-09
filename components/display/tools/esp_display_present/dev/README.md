# present 测试工作区

所有维护中的测试源码、板级配置、运行工具和证据索引统一位于 `esp_display_present/dev`。应用示例仍保留在 examples，供用户接入使用；日常回归、性能测试和 adapter 对比以本目录为入口。

生产源码的职责划分与新增文件规则见[SOURCE_LAYOUT.md](SOURCE_LAYOUT.md)。

## S31 RGB 单tile性能定位（2026-11-09）

[专项数据与证据](evidence/present-s31-rgb-analysis-20261109/comparison.html)：双方单tile，PARTIAL 0°/90°；40项完整16场景对照。双方同开XIP后四项均为37/36/40/40 FPS，相同旋转缓冲容量复核相同；burst与缓存维护精简未带来综合提升。只验证本专项，不替换五板全矩阵成绩；默认配置、生产源码和IDF未修改，原固件已恢复并整片校验。此前双tile尝试停止并从对比中排除。

## S3 FB_IRAM 开关对照（2026-11-09）

[完整24项帧率／内存对照](evidence/present-s3-fb-iram-on-off-20261109/comparison.html)：同版本源码，SDK唯一差别为FB_IRAM y/n。关闭增加内部堆21.36KiB；当前32KiB指令缓存/XIP配置下，FULL/DIRECT基本接近，PARTIAL/AUTO低1～3FPS。关闭只用于隔离补测，组件默认仍开启；原固件整片恢复校验通过。

## 最新五板对照（2026-11-09）

[报告与口径](evidence/five-board-regression-20261109/README.md) · [帧率及四时点内存网页](evidence/five-board-regression-20261109/comparison-current.html) · [Excel](evidence/five-board-regression-20261109/comparison-current.xlsx)。生产源码 `b607bb9a1`，present108项、adapter73项通过，31项明确跳过；每项完整16场景，未拼接历史成绩。RGB/DSI不含NONE，mosaico仅TE，BOX-3测GRAM。五板原固件全部恢复并独立整片校验；新固件没有新增目视/光学防撕裂验收。S3本轮为R16/16MiB PSRAM，MAC已更新，旧R8成绩不混用。

## 目录

```text
dev/
  boards/boards.json             # 型号、接口、分辨率、目标芯片与已确认 MAC
  boards/configs/                # 公共、板级、LVGL 配置，各实现共用
  tests/                        # Host、LVGL binding 与测试工具回归
  test_apps/
    boards/                     # RGB/DSI/QSPI 像素、模式、旋转测试；共享板级初始化
    benchmark/                  # 一份 LVGL benchmark 主程序，两种实现 bridge
      main/main.c               # 场景、评分、清零、超时与日志
      main/bridge_present.c
      main/bridge_adapter.c
      main/te_meter.c            # S31 TE gate/DMA 独立统计
      components/hw_init/        # 复用 boards 的 RGB/DSI/QSPI 初始化，另含 BOX-3
    te_lifecycle/               # S31 TE 旋转、lease/copy/TE/DMA 停止测试
    smoke/                     # IDF API/链接检查，无真实面板
    legacy_compat/             # 历史 V1 接入兼容，不等同原始 adapter 对比
    scanout_boundary/          # 隔离驱动观测专项；需要独立准备，不是默认固件
  tools/run.py                 # 统一日常入口
  tools/dev.py                 # Host/格式检查底层入口，保留兼容
  evidence/README.md           # 历史结果索引、适用版本和已被替代的数据
```

源码在仓库中维护；构建目录、sdkconfig、依赖副本、原始 Flash 备份和本次运行输出全部默认放在 `/tmp/esp-display-present-tests`。长期使用建议用 `--work-root /path/to/persistent-work`，不要将 Flash 备份只保存在 `/tmp`。本轮已归档的源码快照不再作为新测试工程的来源。

P4 `pixels` 套件同时运行 PPA/DMA2D executor 检查：internal/PSRAM source、RGB565/RGB888、四角度能力选择、CPU fallback、逐像素结果，以及两个同类硬件 client 共存和单 client 销毁隔离。Host executor 测试另外核对 source C2M、destination C2M/M2C 的范围、顺序、错误传播和 `NOT_SUPPORTED` 非缓存内存语义。

## 板卡

| board 参数 | 板子 / 接口 | 尺寸 | 日常套件 |
|---|---|---|---|
| s3-rgb | S3 LCD EV R16 / GC9503 RGB bounce | 480×480 | benchmark、pixels、native、smoke |
| p4-dsi | P4 Function EV rev3 / EK79007 MIPI DSI | 1024×600 | benchmark、pixels、native、smoke |
| box3-spi | ESP-BOX-3 / ILI9341 SPI GRAM | 320×240 | benchmark、smoke |
| s31-rgb | S31 Korvo-1 原配 RGB，10 行 bounce | 800×480 | benchmark、pixels、rgb-events、smoke；像素/事件已验；当前防撕裂benchmark六模式×四角度24项通过，adapter20项通过、4项AUTO跳过；旧28项见历史记录 |
| s31-qspi | S31 mosaico / CO5300 QSPI，TE43 | 480×480 | benchmark、pixels、te-lifecycle、smoke |

以 `boards/boards.json` 的 MAC 识别设备，不绑定 USB0/ACM0。增加新板子需要增加明确配置，不能只改端口号。

## 准备与构建

从组件目录执行，先激活干净 IDF。离线依赖从 Component Manager 本地缓存复制，保留许可证并记录源文件 SHA；不下载、不改 IDF。默认使用缓存的 LVGL 9.6.0~1，可通过 `--lvgl-dir` 指定本地源码，双方必须同版。

```sh
python3 dev/tools/run.py list
python3 dev/tools/run.py prepare --work-root /tmp/present-work
python3 dev/tools/run.py host --sanitize --work-root /tmp/present-work
python3 dev/tools/run.py build --board s3-rgb --suite benchmark --implementation present --work-root /tmp/present-work
python3 dev/tools/run.py build --board s3-rgb --suite benchmark --implementation adapter --work-root /tmp/present-work
python3 dev/tools/run.py build --board p4-dsi --suite pixels --work-root /tmp/present-work
python3 dev/tools/run.py build --board s31-qspi --suite te-lifecycle --work-root /tmp/present-work
```

RGB / MIPI DSI 默认 benchmark 矩阵只含防撕裂模式，不含 NONE。NONE 仅供显式 `--profiles NONE` 的专项诊断，不纳入日常对比表。S31 mosaico 默认只测 TE；允许撕裂的 GRAM 配置在 BOX-3 运行。

benchmark 默认四个角度，`--angles 0,90` 可选子集；`--profiles DOUBLE_DIRECT` 或 `--profiles TE_C2_T1,TE_C2_T2` 可选模式。同一次对比双方必须使用完全相同的 profiles 和 angles。pixels/native 使用各自固件内的固定矩阵，`--angles/--profiles` 只作用于 benchmark。

原始 adapter 的运行源码不修改。离线构建在工作目录创建 symlink overlay，只将 CMake 依赖名称 `lvgl__lvgl`/`espressif__esp_lcd_touch` 映射到同版离线组件。运行源码仍指向仓库原始 adapter，测试不会写入它的 src/include 或 IDF。

## 板上运行与原固件恢复

以下命令会访问指定硬件，依次执行；`--port` 必须传当前连接端口。当前板测验证使用 esptool 5.4.0 的 RAM stub（包括 S31），不强制 ROM-only；S31 USB ROM-only 整片读取曾在约60KiB中断。backup 先验证 MAC，再读取并独立校验整片 Flash；不会覆盖已存在备份。flash 必须有这块板的校验通过备份。若更换固件或板子，用新的 work-root 开始。

```sh
python3 dev/tools/run.py backup --board s3-rgb --port /dev/ttyUSB0 --work-root /tmp/present-work
python3 dev/tools/run.py flash --board s3-rgb --implementation present --port /dev/ttyUSB0 --work-root /tmp/present-work
python3 dev/tools/run.py capture --board s3-rgb --implementation present --port /dev/ttyUSB0 --work-root /tmp/present-work
python3 dev/tools/run.py report --board s3-rgb --implementation present --work-root /tmp/present-work
# 换 adapter 后重复 flash / capture / report，参数 --implementation adapter
python3 dev/tools/run.py compare --board s3-rgb --work-root /tmp/present-work
python3 dev/tools/run.py restore --board s3-rgb --port /dev/ttyUSB0 --work-root /tmp/present-work
```

capture 重置后启动一次测试并保存完整串口，默认超时40分钟，足够最长默认 benchmark；可用 `--timeout` 调整。报告要求结束标记、完整矩阵且无测试错误，部分完成不会记通过。restore 写回整片备份，保持 flash 参数，独立校验后复位，记录 `backup/restored.json`。

## 结果与对比口径

每轮输出按 `<work-root>/<board>/<suite>/<implementation>/` 隔离：`manifest.json`、`build.log`、`live.log`、`results.json`。manifest 记录 IDF commit、LVGL 场景源码指纹、公共配置指纹、实际矩阵和固件 SHA。比较前核对板子、矩阵、IDF、LVGL benchmark 源码和关键 sdkconfig，一项不一致就停止，不生成混合版本分数。

`comparison.csv`/`comparison.md` 位于 `<board>/benchmark/`。只有明确 SKIP 的组合写 `-`，漏跑写“未测”并阻止标记完整。AUTO、adapter NONE 软件旋转及无法表达的 C/T 组合不做替代测试，不用硬件旋转冒充软件旋转。

S31 的 C1/C2 × T1/T2 是 present 的显式 compose/tile 矩阵，adapter 没有同义配置入口，这些组合记 `-`；这不代表 adapter 本身没有通用 TE_SYNC 功能。若以后增加通用 TE 基线，应另列 profile，不能冒充 C/T 对照。

TE wait 计时从已可发送图像第一次请求 gate 到实际放行，包含重试等待，不包含前序 DMA 排队/复制/绘制；DMA 独立统计。LVGL FPS 是有效场景均值，整轮提交 FPS 是另一指标；adapter 未提供传输计数，不能把占位0解释为0吞吐。图像效果由目视反馈另行记录。

## 专项入口与历史

Host / smoke / pixels / native / benchmark / te-lifecycle 由统一工具直接运行。legacy_compat 和 scanout_boundary 仍在本目录维护，分别见各自 README；它们需要历史组件或 IDF 隔离副本，未混入默认无补丁固件。`experimental/` 和 `test_apps/driver_patches/` 属于历史实验材料，不作为当前验收入口。

旧 examples 工程及本机临时目录保留，避免破坏用户改动或原始 Flash 备份；后续测试不再从 `/tmp` 找工程。结果版本和当前有效记录见 [证据总索引](evidence/README.md)。本次整理的构建与 Host 验证见 [整理验证](UNIFIED_TESTS_20261106.md)。

组件根目录的 `test_apps/` 保留 ESP-IDF 标准 Unity/pytest 测试布局，供组件 CI 自动发现；它也是 present 目录的一部分，不迁移其 CI 入口。日常 Host/硬件矩阵仍从上面的统一工具进入。

## 可选分阶段统计

`build/flash/capture/report` 增加 `--profile` 可构建、运行统计版 present benchmark，输出隔离在 `present-profile/`，不会覆盖普通基线。示例：

```sh
python3 dev/tools/run.py build --board s3-rgb --profiles NONE --angles 0 --profile --work-root /tmp/present-work
```

统计 engine 的 begin/acquire/submit/commit 调用壁钟时间、executor_run CPU/硬件/回退次数与有效像素字节数、缓存维护范围、panel 提交时间；S31 TE 等待和DMA另有独立统计。各阶段存在嵌套、并行及互斥等待，不能将总耗时相加或把 executor_run 当成纯计算时间。缓存字节是请求维护范围，像素字节是成功请求的有效payload，都不是实测PSRAM总线流量。统计只存在于测试工程的链接包装器，正常构建不启用。统计版不参与普通 `compare`，用于定位后需用无统计基线确认性能。

对比工具同时核对公共 defaults 和共享测试源码，sdkconfig 指纹包含指令/数据缓存、LCD 与显式禁用选项；组件私有 Kconfig 不作为双方共同配置。RGB/MIPI DSI NONE 当前均为 PARTITION：present 管理tile，驱动管理内部framebuffer；旧证据中的present NONE DIRECT成绩只描述当时源码。分段统计的 executor 次数不包含 LVGL binding 内的 memcpy，次数为0不代表整条路径零拷贝。

架构重构进度及当前结构见 [架构收敛记录](ARCHITECTURE_REFACTOR_20261106.md)。各批按接口、文件职责、配置解析和状态归属逐步推进。


## 综合场景内存统计

benchmark在共用主程序的场景边界采样，不添加逐帧日志。每组记录before_open、after_open、after_run、after_close，另记录整套bench_init前后。内部RAM使用INTERNAL|8BIT，PSRAM使用SPIRAM|8BIT，两个口径不重叠。

打开占用为after_open的total-free，打开增量为before_open到after_open的free变化；它们包含测试环境、任务栈和LVGL分配，不是单独组件的最小内存预算，也不包含链接时静态段。S3启用PSRAM XIP时还须结合heap总量和剩余量判断代码预留的影响。

局部最小剩余从open前监控到benchmark结束、close前，覆盖创建和整个综合场景。IDF按各heap历史低水位求和，不保证发生在同一时刻，不能由此宣称精确的瞬时峰值。关闭后的free用于观察回收，不单凭一次差值认定泄漏。

results.json保留分阶段原始字节，comparison.csv在FPS列后追加两实现的heap总占用、打开增量和最小剩余。新manifest的memory_schema=1要求所有有效组合具备完整采样；旧日志仍可解析，但不同memory schema不能混合比较。FPS仍为16个有效LVGL场景的均值，不是面板刷新率；不完整或错误场景不能采用其高分。

## S31 Korvo RGB 事件专项

USB3 新接板以 MAC `30:ed:a0:f3:f7:b8` 识别，原配 800×480，18 MHz。
`--board s31-rgb --suite rgb-events` 使用干净 IDF 原生驱动，覆盖 bounce 0/10 行、真实 1/2/3 framebuffer、提交 core0/core1 共 12 组。
记录静止期间 frame-complete / VSYNC 次数，以及提交后的等待时间。驱动提供 framebuffer capability 查询时，每组还会核对物理尺寸、像素格式、FB 数量/容量、continuous refresh、bounce 实际容量并记录 retirement 能力；配置事实不一致会立即失败。单 framebuffer 为允许撕裂的 active-write 测试。
这是驱动事件观测，不代表 present 已支持 non-bounce，也不能单凭回调数证明像素没有撕裂。

2026-11-07 实测：[S31 Korvo RGB 补测记录](evidence/s31-rgb-korvo-20261107/README.md)。
28组像素/角度检查、12组原生驱动事件、四个0°防撕裂模式 present/adapter 综合场景完成。
本轮没有把 non-bounce 接入生产 present；原生事件测试结果不能替代该适配实现及后续压力验证。

## S31 QSPI adapter 原生 TE 基线

此前 C1/C2×T1/T2 表仅测试 present，不代表 adapter 不支持 TE。
使用 `--board s31-qspi --implementation adapter --profiles ADAPTER_TE_SYNC_FULL,ADAPTER_TE_ASYNC_FULL,ADAPTER_TE_ASYNC_BOUNCE` 构建补测；默认四角度，同步非0°明确跳过。
SYNC_FULL 为单全屏绘图；ASYNC_FULL 为全屏DIRECT绘图加全屏staging；ASYNC_BOUNCE 再加两个16行传输bounce。
TE 路径不通过 require_double_buffer 选择 present 式 T1/T2，不强行映射同一 C/T 名称。
原生 adapter 基线用 report 单侧归档，不能伪造同名 present 用例来运行 compare。

本轮adapter TE补测结果见[evidence/s31-adapter-te-baselines-20261107](evidence/s31-adapter-te-baselines-20261107/README.md)。
2026-11-07的恢复中断已完成恢复和校验。2026-11-09五板重测也已全部恢复原固件并整片校验，当前状态以最新记录为准。

## DIRECT 优化与 S31 RGB 完整对照（2026-11-07）

[完整记录](evidence/direct-borrowed-20261107/README.md)：present 一次连续完成28项，原始adapter一次完成21项支持组合并跳过7项。每项完整16场景。0° DIRECT从20提升到23 FPS，原始adapter本轮24 FPS；旋转保留copy路径。源码提交 `4a9daccee`，IDF和原始adapter未修改。

benchmark 的 present 收尾会检查LVGL绑定保存的错误码，防止绘制暂停但场景计时器结束被误记PASS。插桩轮独立保存，不能替代正式跑分。打开前两实现的首次框架初始化边界不同，跨实现比较应优先看打开后、运行最小和关闭后堆余量。
