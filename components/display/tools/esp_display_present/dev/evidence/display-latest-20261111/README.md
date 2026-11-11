# 五板最新显示对比 — 2026-11-11

[打开网页](comparison.html) · [CSV](comparison.csv) · [网页数据JSON](comparison.json) · [原始汇总](matrix-data.json)

覆盖S3 RGB480×480、S31 Korvo RGB800×480、P4 MIPI DSI1024×600、S31 MOSAICO QSPI TE480×480、ESP-BOX-3 SPI320×240。五板顶部Tab，四角度帧率并排，内存按模式及剩余/已用选择，诊断与场景按抽屉展开。剩余越多越好；差值放present单元格中。

这次更新网页，不是宣称五板全部使用39156930d重新跑测。181项有效性能/内存记录，130项完整独立诊断记录，合计4976个场景。诊断另有2项修复前未完成记录，保留而不当作修复后失败。没有补造缺失成绩。

- RGB/MIPI只用XIP ON，不加入NONE；S3开bounce，S31 RGB非bounce。使用11-11最近完整记录及此前已核对可复用基线。
- P4五项已换为39156930d修复后回归：DOUBLE_DIRECT270、DOUBLE_PARTIAL90/270、TRIPLE_PARTIAL90、AUTO270。其余记录注明修复前来源。地址观察仍是修复前诊断；不能把普通回归通过当作重新检测地址为零。
- TE present16项来自11-11的带顶部模式标签复测；adapter9项来自最近原生配置基线。C/T与SYNC_FULL/ASYNC_FULL/ASYNC_BOUNCE分别列出，不强行定义等价，不计算不同配置差值。标题负载不同，不作严格跨实现性能结论。串口不证明无脏块或撕裂。
- SPI使用最近归档：单tile行容量对齐记录与双tile/AUTO最近完整记录。此轮未重新烧录SPI。GRAM路径允许撕裂，不支持的软件旋转与AUTO记−。

静态SRAM来自归档ELF与实测公共启动堆；P4 present和TE present已提取本次固件，SPI从匹配归档二进制的单tile构建提取。TE adapter缺ELF的静态项记−；不从堆剩余倒推、不假定扣除JPEG。整套固件账目不是组件独占占用。各模式的四时点剩余和运行最小值保留原始bytes。

复现网页（无硬件操作）：

```sh
python dev/tools/build_latest_dashboard.py --evidence-root dev/evidence --accounting dev/evidence/display-latest-20261111/memory-accounting.json --output /tmp/display-latest/matrix-data.json
python dev/tools/render_comparison.py --data /tmp/display-latest/matrix-data.json --output /tmp/display-latest/comparison.html
```

在esp_display_present目录执行。源记录的路径和SHA256见source-index.json。旧目录中的其他文件仍保留各自历史记录，当前网页下载链接才对应本次汇总。
