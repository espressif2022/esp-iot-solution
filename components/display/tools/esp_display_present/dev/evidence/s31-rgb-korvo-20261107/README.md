# S31 Korvo 原配 RGB：接口补测与 adapter 对照

板卡 MAC `30:ed:a0:f3:f7:b8`，本轮端口 USB3。原配 RGB565 800×480，PCLK 18 MHz，CPU 320 MHz / PSRAM 200 MHz，IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。不修改生产组件、adapter 或 IDF。

## 覆盖与结果

- present RGB bounce：7 模式 × 4 角度，28 组像素参考检查通过；每组包含局部更新与一次运行中旋转检查。驱动公共池固定三个 framebuffer，不把 NONE_1FB 名称当作实际 panel 分配数量。用户在像素 / 事件测试阶段反馈显示正常。
- 原生 RGB 驱动事件：bounce 0/10 行 × 实际 1/2/3 framebuffer × 提交 core0/core1，共 12 组，每组 80 次提交通过。面板和中断在 core0 创建。单 framebuffer 主动写扫描中内容，允许撕裂。
- present / adapter LVGL benchmark：DOUBLE_FULL、TRIPLE_FULL、DOUBLE_DIRECT、DOUBLE_PARTIAL，0°各四组，16 个有效场景；连续创建与关闭完成。不是全模式四角度 benchmark，不代表长时间压力验证。
- 共用测试工具 15 项 unittest 与格式检查通过。

## 完成事件差异

每组不提交画面，静止观察 250 ms：non-bounce 的 framebuffer-complete 次数集合为 [0]；bounce 为 [10]；各配置 VSYNC 次数集合为 [10]。逐组数据见 event-summary.md。

S31 non-bounce 走 AXI GDMA link-switch 通知，不能当作周期完成事件。现有 present 的 RGB 适配在干净 IDF 下仍拒绝 non-bounce。本轮 direct 结果来自隔离原生驱动探针，不是 present non-bounce 的支持或防撕裂验收。原生事件不携带 framebuffer 地址，因此回调数与未超时不能独立证明所有时序下均无提前复用。

bounce 探针每次等待两个周期事件以保守覆盖提交竞态，direct 每次等待一次请求事件；两者等待均值不能直接当作吞吐或 FPS 优劣对比。

## LVGL FPS（0°，RGB bounce 10 行）

| 模式 | present（相对 adapter） | adapter |
|---|---:|---:|
| DOUBLE_FULL | 22（-1） | 23 |
| TRIPLE_FULL | 25（-2） | 27 |
| DOUBLE_DIRECT | 20（-3） | 23 |
| DOUBLE_PARTIAL | 17（-2） | 19 |

每格单次有效测量，LVGL FPS 非屏幕物理刷新率。adapter 没有等价的 transport frame 计数，其日志 frames=0 / transport_fps=0 不代表没有出帧。配置、LVGL 和共享源码指纹一致，详见两个 manifest 与 comparison.csv。

## 内存余量（KiB）

RGB 公共初始化先分配三个 panel framebuffer，再逐模式打开显示实例。下表四时点为显示实例的 before_open / after_open / after_run local minimum / after_close，不含整个框架反初始化，不单独统计静态数据区。RAM 与 PSRAM 互不重叠。

| 模式 | 时点 | present RAM | adapter RAM | present PSRAM | adapter PSRAM |
|---|---|---:|---:|---:|---:|
| DOUBLE_FULL | 打开前剩余 | 387.96 | 344.84 | 14131.22 | 14131.22 |
| DOUBLE_FULL | 打开后剩余 | 342.92 | 334.38 | 13378.24 | 14125.50 |
| DOUBLE_FULL | 运行最小剩余 | 256.69 | 248.89 | 12960.20 | 13775.47 |
| DOUBLE_FULL | 关闭后剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| TRIPLE_FULL | 打开前剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| TRIPLE_FULL | 打开后剩余 | 342.94 | 334.46 | 13378.24 | 14125.50 |
| TRIPLE_FULL | 运行最小剩余 | 257.38 | 248.67 | 12960.20 | 13835.47 |
| TRIPLE_FULL | 关闭后剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| DOUBLE_DIRECT | 打开前剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| DOUBLE_DIRECT | 打开后剩余 | 342.98 | 334.38 | 13378.24 | 14125.50 |
| DOUBLE_DIRECT | 运行最小剩余 | 263.26 | 254.56 | 13088.21 | 13869.48 |
| DOUBLE_DIRECT | 关闭后剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| DOUBLE_PARTIAL | 打开前剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |
| DOUBLE_PARTIAL | 打开后剩余 | 264.69 | 256.33 | 14050.24 | 14125.50 |
| DOUBLE_PARTIAL | 运行最小剩余 | 210.81 | 195.41 | 13938.23 | 14012.49 |
| DOUBLE_PARTIAL | 关闭后剩余 | 366.13 | 341.53 | 14131.22 | 14128.12 |

## 原固件与限制

原始16 MiB Flash先完整备份并独立verify-flash，备份持久保存在客户工作区 hardware-backups/s31-korvo-30eda0f3f7b8-20261107。测试完成后全片恢复并再次verify-flash；校验日志随证据保留。

当前结果不覆盖 cache-off / Flash擦写并发、长期高带宽竞争、S31 non-bounce 的 present 交接实现、P4 RGB 或S3 non-bounce。用户目视正常反馈不扩展为全部 benchmark 场景的独立目视验收。
