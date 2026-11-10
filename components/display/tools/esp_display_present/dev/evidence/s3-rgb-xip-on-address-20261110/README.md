# S3 XIP ON 轻量地址诊断

[检测表](comparison.html) · [CSV](comparison.csv) · [JSON](matrix-data.json)

0°，11项支持模式、每项完整16场景，共176场景。adapter AUTO不支持，NONE排除。PCLK16MHz，十行bounce，CPU240MHz，PSRAM80MHz，XIP/FETCH/RODATA开启。公共有效SDK配置逐项相同。

Live driver bb_fb_index is independently read at software ownership transitions, not compared after assignment.

Only explicit release/acquire/confirm and LVGL render-start boundaries covered; zero is not proof against arbitrary CPU/DMA writes or every visible fault.

Pool-external input tiles are counted but not errors. Cross-switch source read is separate. Source/target/position are not a fully atomic struct snapshot; target is context only.

Observer takes short critical sections, changes scheduling slightly; no pixel hashes, asserts, corrections, callback replacement or delays. Diagnostic FPS/memory not formal performance data.

RUN begins on next VSYNC, leading partial frame and close excluded; benchmark summary logging included.

Host ASan/UBSan验证：正常交还不报错；交还/取得仍在读的FB能检出；即使软件target匹配而实际source不同也能检出确认异常；错误池指针、跨切换未知值、阶段门控和reset区分记录。正式checkout和基础IDF不改，只有影子present/adapter和隔离esp_lcd观察副本。

全量内容指纹版本产生明显供数负担，已停止。其首项完整DOUBLE_FULL0有2112/4201次不足，195804段指纹无变化，不能作为原程序错误率或无踩踏证明。同期轻量对照单项完整16场景另附；不是伪称整套跑完。

按用户要求不恢复原固件，保留adapter XIP ON地址诊断固件。归档无build和固件镜像。
