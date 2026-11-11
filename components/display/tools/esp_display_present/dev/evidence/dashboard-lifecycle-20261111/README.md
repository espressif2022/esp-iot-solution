# 当前比较网页更新 · 2026-11-11

[当前网页](../five-board-regression-20261109/comparison-current.html) 使用维护中的 renderer，通过 `--lifecycle` 接入本轮三板 24 项数据；未重跑硬件。adapter、其他 present 项、ELF 静态账目和独立诊断保留历史来源，新旧日期显示在成绩单元格内。每项新测完整 16 场景，逐场景分数从对应的最终 live.log.gz 提取。

输入：`rgb-mipi-xip-on-no-fb-iram-20261110/matrix-data.json` 和 `scanout-lifecycle-cleanup-20261111`。本轮原始证据未改；新的 CSV/JSON 与当前网页一起生成。新增对上一版 present 的打开 PSRAM 增量对照和实际提交吞吐；adapter 没有提交吞吐统计，显示横杠。没有把历史驱动诊断当作新固件的检测结果。

`validation.json` 保存输出及维护工具 SHA256。通过 Chromium CDP 核对 4356 个数值、24 个新测来源、36 次模式选择、板卡 Tab/抽屉和 1440/390 像素布局，浏览器异常为零；raw-evidence 检查确认所有新测结果来自最终原始结果，历史 adapter 与诊断未变。截图仅为本页排版记录。

复核交互：启动本机 Chromium `--remote-debugging-port=9333`，运行 `node dev/tools/check_comparison.mjs <comparison-current.html 绝对路径> 9333 /tmp`。脚本只读测试网页，无串口操作。
