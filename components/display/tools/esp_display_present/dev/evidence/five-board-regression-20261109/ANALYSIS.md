# 同容量重测结果

以下是双缓冲／三缓冲 × 四角度共8项LVGL综合分的均值，非真实扫描FPS。每项包含16场景，只有一轮；小幅分差不视为统计显著。

| 板卡 | 组 | present | adapter | present相对差值 | 非空绘制/s present / adapter |
|---|---|---:|---:|---:|---:|
| S3 RGB · IRAM on | FULL | 21.50 | 19.88 | +8.2% | 19.67 / 18.10 |
| S3 RGB · IRAM on | PARTIAL | 44.38 | 42.00 | +5.7% | 30.41 / 29.74 |
| S3 RGB · IRAM off | FULL | 21.38 | 19.88 | +7.5% | 19.46 / 18.10 |
| S3 RGB · IRAM off | PARTIAL | 41.88 | 42.00 | -0.3% | 28.72 / 29.74 |
| S31 RGB | FULL | 20.00 | 19.25 | +3.9% | 17.63 / 17.00 |
| S31 RGB | PARTIAL | 19.75 | 23.50 | -16.0% | 15.04 / 17.78 |
| P4 DSI | FULL | 19.38 | 18.25 | +6.2% | 15.94 / 15.01 |
| P4 DSI | PARTIAL | 45.38 | 48.50 | -6.4% | 32.78 / 35.34 |

本轮结果不支持“差距全由tile大小引起”：S31、P4的PARTIAL在容量与内存位置一致后仍落后，而FULL的不同缓冲模式表现不同。应继续按逐场景绘制、复制/合成和等待路径分析；本轮不据此宣称已证明具体瓶颈。

对齐采用present原本的逻辑宽×50行策略，缩小adapter在90°/270°的输入容量；没有给present加大tile。旧新测试顺序及核对代码不同，因此不能把旧新分差全部解释成tile容量的因果影响。

S3保持原XIP开启，S31保持双方XIP关闭，P4保持原配置；这是各板内部的同配置比较，不能用跨板分差证明某一种芯片机制更优。BOX-3只有0°能双方等价比较，其余角度adapter不支持；mosaico TE保留历史，不混入本轮均值。

剩余内存差值范围（present − adapter，打开后，KiB，正数表示present剩余更多；范围覆盖该组8项）：

| 板卡 | 组 | 内部RAM | PSRAM |
|---|---|---:|---:|
| S3 RGB · IRAM on | FULL | -15.45 ～ -15.36 | -898.91 ～ -448.90 |
| S3 RGB · IRAM on | PARTIAL | -15.56 ～ -15.55 | -39.90 ～ -39.90 |
| S3 RGB · IRAM off | FULL | +5.91 ～ +6.00 | -899.08 ～ -449.07 |
| S3 RGB · IRAM off | PARTIAL | +5.80 ～ +5.81 | -40.07 ～ -40.07 |
| S31 RGB | FULL | +8.28 ～ +8.36 | -1497.26 ～ -747.26 |
| S31 RGB | PARTIAL | +8.16 ～ +8.17 | -75.26 ～ -42.26 |
| P4 DSI | FULL | +8.71 ～ +8.82 | -2411.27 ～ -1211.26 |
| P4 DSI | PARTIAL | +8.55 ～ +8.59 | -97.26 ～ -54.26 |

该差值是两个固件在该时点的整堆剩余之差，包含运行存储及固件布局影响，不等于单独组件的动态分配；打开增量还会受到首项LVGL初始化边界不同的影响。容量核对只证明活跃输入一致，未把present的保底存储和旋转画布人为填平。


## s3-rgb PARTIAL逐场景均值

| 场景 | present | adapter | 分差 |
|---|---:|---:|---:|
| Multiple labels | 48.75 | 56.50 | -7.75 |
| Screen sized text | 10.00 | 9.75 | +0.25 |
| Multiple arcs | 60.00 | 59.50 | +0.50 |
| Widgets demo | 13.25 | 12.62 | +0.62 |
| Moving wallpaper | 27.75 | 27.00 | +0.75 |
| Single rectangle | 61.00 | 60.00 | +1.00 |
| Containers with scrolling | 20.88 | 19.25 | +1.62 |
| Containers with overlay | 21.00 | 19.25 | +1.75 |
| Rotated ARGB images | 63.12 | 61.12 | +2.00 |
| Empty screen | 30.25 | 26.75 | +3.50 |
| Multiple RGB images | 63.62 | 59.50 | +4.12 |
| Multiple ARGB images | 63.00 | 58.62 | +4.38 |
| Containers | 65.12 | 59.75 | +5.38 |
| Containers with opa | 64.38 | 58.00 | +6.38 |
| Multiple rectangles | 56.75 | 49.38 | +7.38 |
| Containers with opa_layer | 47.12 | 39.38 | +7.75 |

## s31-rgb PARTIAL逐场景均值

| 场景 | present | adapter | 分差 |
|---|---:|---:|---:|
| Multiple arcs | 24.00 | 40.00 | -16.00 |
| Multiple rectangles | 27.75 | 35.75 | -8.00 |
| Containers with opa | 30.00 | 36.12 | -6.12 |
| Containers | 32.12 | 37.88 | -5.75 |
| Multiple labels | 15.25 | 19.12 | -3.88 |
| Multiple ARGB images | 15.38 | 19.00 | -3.62 |
| Multiple RGB images | 20.12 | 23.50 | -3.38 |
| Containers with opa_layer | 11.62 | 14.62 | -3.00 |
| Screen sized text | 5.75 | 8.62 | -2.88 |
| Empty screen | 19.12 | 21.88 | -2.75 |
| Rotated ARGB images | 29.50 | 31.50 | -2.00 |
| Containers with overlay | 11.00 | 12.50 | -1.50 |
| Containers with scrolling | 9.12 | 10.38 | -1.25 |
| Moving wallpaper | 12.88 | 14.12 | -1.25 |
| Single rectangle | 41.62 | 41.75 | -0.12 |
| Widgets demo | 17.50 | 17.12 | +0.38 |

## p4-dsi PARTIAL逐场景均值

| 场景 | present | adapter | 分差 |
|---|---:|---:|---:|
| Multiple ARGB images | 46.88 | 59.50 | -12.62 |
| Rotated ARGB images | 48.12 | 57.75 | -9.62 |
| Multiple labels | 50.50 | 60.00 | -9.50 |
| Moving wallpaper | 42.75 | 49.25 | -6.50 |
| Containers with scrolling | 27.62 | 33.50 | -5.88 |
| Containers with opa_layer | 34.88 | 39.75 | -4.88 |
| Multiple arcs | 39.00 | 43.00 | -4.00 |
| Containers with overlay | 26.88 | 28.75 | -1.88 |
| Widgets demo | 38.25 | 38.75 | -0.50 |
| Screen sized text | 14.38 | 14.50 | -0.12 |
| Empty screen | 53.00 | 53.00 | +0.00 |
| Multiple rectangles | 60.25 | 60.00 | +0.25 |
| Multiple RGB images | 60.25 | 59.75 | +0.50 |
| Single rectangle | 61.25 | 60.00 | +1.25 |
| Containers | 65.00 | 63.38 | +1.62 |
| Containers with opa | 64.75 | 62.62 | +2.12 |

S31差距最明显的并非只限整屏填充：Multiple arcs为24.00/40.00，而Moving wallpaper为12.88/14.12。P4的Multiple ARGB images为46.88/59.50，但Screen sized text为14.38/14.50。它们只定位到场景差异，不能单凭LVGL分数断言是合成、cache或提交中的某一环。
