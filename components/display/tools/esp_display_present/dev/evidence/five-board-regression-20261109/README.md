> comparison-current.html是持续更新入口，现覆盖RGB/MIPI/TE/SPI五板。2026-11-11更新：[最新网页](comparison-current.html)、[CSV](comparison-current.csv)、[JSON](comparison-current.json)。来源与测试条件逐项标注；完整说明见[最新汇总](../display-latest-20261111/README.md)。旧XLSX和下列说明属于11-09历史，不代表本轮五板全部重跑。

## 以下为2026-11-09历史说明

# present / adapter 五板对比（2026-11-09 更新）

[同网页对照](comparison-current.html)：S3 IRAM开／关独立表，帧率、容量、四时点剩余内存均并排。

最新更新是[同容量FULL/PARTIAL重测](../present-aligned-fill-20261109/README.md)。122项有效成绩、6项跳过，含S3 IRAM开／关、S31 RGB、P4 DSI和BOX-3单tile；每项16场景。RGB/DSI不含NONE。DIRECT/AUTO、mosaico TE和GRAM双tile保留历史，表内标明，不能称全部为本轮重跑。

PARTIAL双方按逻辑宽×50行×RGB565核对：S3 48000B；S31 0/180°80000B、90/270°48000B；P4分别102400B/60000B。adapter仅在隔离测试副本改变分配表达式，交接/复制/同步及IDF未改。固定单输入tile，未启用双tile。FULL检查单张整帧容量；额外像素存储仍计入内存。

[结果分析](ANALYSIS.md)按FULL/PARTIAL列出组均值，详项、逐场景及内存仍在网页。剩余内存越多越好，CSV/JSON保留原始bytes。LVGL分数和非空绘制/s都不等于实际扫描完成率；CPU100%不作真实负载。

comparison-current.json/csv/xlsx为合并新旧数据，cohort明确区分。本目录aligned-raw-results.json、aligned-manifests.json、buffer-verification.json只涉及新测；原始raw-results.json/manifests.json、evidence及*-before-alignment文件保留历史，不代表最新重测。完整新日志、配置、恢复证明在相邻present-aligned-fill-20261109目录。最新validation/file-validation/browser-validation针对本轮更新，历史验证另存before-alignment。

四块重测板均恢复运行前整片固件并独立校验；mosaico本轮未操作。P4原固件含旧NONE用例，恢复后可能再次显示，与本轮排除NONE的矩阵区分。串口跑完不替代目视防撕裂验收。

## S31 取指停顿与派发定位

[根因定位](../present-s31-dispatch-20261109/root-cause.html)：24项完整16场景及38项定点运行，固定单tile、同容量；验证代码排列对字体解包取指缓存的影响，记录PC热点与约1KiB单函数IRAM对照。原固件已恢复并整片校验。本页原性能和内存矩阵不替换。
