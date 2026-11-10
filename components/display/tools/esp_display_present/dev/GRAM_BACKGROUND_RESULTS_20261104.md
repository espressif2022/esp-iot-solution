# GRAM 后台提交首版（2026-11-04）

## 结论与提交状态

测试前已提交当前组件基线：`c260e7eb7`（包含此前 FB 吞吐修复及证据）。本次原型基于该提交，默认关闭实验开关。

首版证明可以复用现有 engine sender，为普通内部 tile 的 GRAM 提供后台发送，不强制引入异步像素复制；驱动调用阻塞时，生产者仍可取得另一块空闲 tile。收益主要是 **submit 阶段缩短**，普通双 tile 的总吞吐只提升约 1.2%，尚不能据此默认启用或宣称 LVGL 提帧。

最终 factory 只为 `async_copy == false` 的内部 tile 路径启用新策略。旋转／PSRAM 原本已有异步拷贝与后台推进，继续采用原有调度；早期尝试将它们一起切换到新的提交方式出现回退，原始记录保留。

## 最终实板对照

S31、CO5300、480×480 QSPI 40 MHz、RGB565、16 行 tile；同一生产者源码、相同原版 IDF 驱动，生产者优先级 6。最终实验 sender 优先级为 7。每项预热 8 帧，连续提交 60 帧并排空，重复三次；各变体各启动一次，表中为三个窗口的吞吐算术平均。

| 配置 | 基线次／秒 | 首版次／秒 | 首版调度 |
| --- | ---: | ---: | --- |
| 内部 tile，单 tile，0° | 22.22 | 22.22 | 新后台提交 |
| 内部 tile，双 tile，0° | 37.13 | **37.59** | 新后台提交 |
| 内部 tile，双 tile，90° | 35.97 | 36.46 | 原有异步拷贝调度，对照项 |
| PSRAM tile，双 tile，0° | 36.73 | 37.38 | 原有异步拷贝调度，对照项 |

后两项没有启用新的提交策略，不能把小幅差异归为该策略的收益；本轮样本也不足以证明约 1% 的差异在其他系统负载下稳定存在。这是合成提交吞吐，不是 LVGL benchmark 或光学帧率。

普通双 tile／0° 的调用方阶段计时：

| 指标 | 基线 | 首版 |
| --- | ---: | ---: |
| submit 累计 ms／帧 | 23.51 | **4.04** |
| acquire 累计 ms／帧 | 1.30 | **20.78** |
| 最大单次 submit μs | 809 | **200** |
| 创建期间内部堆减少量 B | 36,072 | **40,408** |

submit 累计耗时下降约 82.8%，但大部分等待迁移到 buffer 背压：源 tile 要等真实发送完成才能再次租用。内部 RAM 增量 **4,336 B，约 4.23 KiB**，主要是原本没有的 sender 任务；没有为该路径额外分配 converted tile。单 tile 没有绘制／发送重叠空间，本轮吞吐持平。

内部堆数值是创建前后 `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT` 的可用量差，包含分配器和任务开销，不是完整系统峰值。

## 实现

1. [资源策略](../src/present_resources.c)：后台推进独立于 `async_copy`，实验 factory 使用 `deferred = !async && experiment_enabled`。自动 tile 数量按是否需要后台重叠选择，显式 tile 数量仍优先。
2. [GRAM backend](../src/backend/present_backend_region.c)：同步像素准备完成后，将原有 slot 发布为 READY，持有 transfer 引用并唤醒 sender。没有增加第二份输出队列或 SPI adapter 任务。
3. [引擎提交包装](../src/present_engine.c)：仅该实验策略允许实际 driver call 期间释放 engine gate。调用前原有 slot 已进入 SUBMITTING；调用返回后先重新取得 gate，再由已有 transaction 机制合并早到回调与返回结果。
4. 只有 sender 能真正调用 driver，前台 acquire／query 引起的 service 只触发重试通知；这保证 gate 释放期间仍只有一个提交者。进入 closing 后不再发出排队的新 tile。
5. stop 先 join sender，再 quiesce adapter。超时保留上下文和像素；`TX_UNCERTAIN`、超时故障本身都不构成 DMA 已结束的证据。

普通零转换路径仍可能进行原有的 RGB565 原地字节序处理；这里的“无需额外复制”指不为取得后台发送能力强制复制到独立 converted buffer。提交返回表示软件接收了该 tile，真实完成仍由回调／quiesce 确认。

## 优先级对照及范围收敛

最初对全部 GRAM 路径启用新提交方式，并沿用默认 sender 优先级 5：普通双 tile 由 37.13 降到 33.31；旋转和 PSRAM 也有回退。

随后在同一诊断固件中分别使用 sender 优先级 6、7（生产者固定 6）：普通双 tile 在 6 时仍约 33.32，在 7 时恢复到约 37.54。这表明后台调度与生产者的优先级关系不可忽略；未采集 RTOS 调度轨迹，不能进一步把差额全部量化为某一个锁或任务切换。

全路径启用优先级 7 的独立固件中，90° 仍出现约 32 次／秒的回退，因此最终 factory 没有将已存在的异步转换路径一起切换。各阶段数据分别记录在 `results.json` 的 `candidate`、`priority`、`final` 和最终的 **`bounded`** 项下，不能混合计算最终均值。

## 配置与验证

```text
CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND=y
CONFIG_ESP_DISPLAY_PRESENT_GRAM_BACKGROUND_PRIORITY=7
```

修改配置后执行 `idf.py reconfigure build`。优先级 7 是本次生产者优先级 6 的实验配置，不自动继承或跟随生产者优先级。实验开关默认 n。

Host＋LVGL **36/36**，ASan／UBSan **36/36**，后者使用 `detect_leaks=0`，没有验证泄漏检测。默认 factory 配置关闭；scheduler suite 显式启用内部 deferred 模式，新增四组行为回归：

- driver call 阻塞期间仍可取得并提交另一块 tile；stop 超时不销毁上下文，也不再发送排队 tile。
- 零复制源地址、传输容量背压、FIFO 和提交返回前完成回调。
- 不确定提交错误后仍持有源像素，直到真实完成。
- driver call 尚未返回时发生传输超时，仍保持源像素和上下文存活。

实板使用 IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 的原版 `esp_lcd`，该目录 git diff 为空。所有变体均完成预期样本、排空及销毁；没有面板 GRAM 读回或光学采样。TE／FB 只有 Host 回归，本轮没有重测其实板吞吐。

每次板测后恢复原镜像，并独立校验完整 16 MiB flash。首次备份校验及基线恢复校验各遇到一次 USB I/O 中断，独立重试均匹配；原始失败与重试记录保留。后续变体恢复实际改写的 1 MiB 前缀，再校验整片匹配。

[原始证据](evidence/gram-background-20261104/README.md)包含源码、配置、固件、实现补丁、完整样本、优先级对照、Host 日志与恢复校验。设备备份含原设备数据，不归档。
