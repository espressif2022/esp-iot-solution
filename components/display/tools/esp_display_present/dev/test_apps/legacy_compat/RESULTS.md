# C2 基础兼容入口验收（2026-11-02）

后续修正与最新结果见 [C2 第二轮](RESULTS_C2_CONTRACTS.md)；本文件保留第一切片的历史验收口径。

本轮未连接/烧录设备。未更新示例默认依赖，没有提交 git commit。测试只针对当前工作区代码；不能沿用此前三板画面或性能数据作为本轮结果。

| 验收项 | 结果与边界 |
| --- | --- |
| Host + LVGL | 33/33 测试条目通过；含既有四种执行器构建组合 |
| ASan / UBSan / LSan | 33/33，`ASAN_OPTIONS=detect_leaks=1` |
| legacy_facade 重复 | 最终代码连续 50 次通过；1 ms stop join 的超时按协议重试，不删在途缓冲 |
| 同名符号 / 头文件 | 21 个 V1 符号导出；三份 presenter 头文件及 LVGL 头文件与原件逐字节一致 |
| S3 / P4 组件离线 smoke | 通过；强制链接兼容 presenter/LVGL/平台资源层，未运行硬件 |
| P4 原 `gsp_lvgl_present_handoff` | 完整离线链接通过；实际使用缓存 GSP 1.9.0 prebuilt，map 指向新 facade；C/C++ 业务源文件逐字节一致 |
| P4 原 `lvgl_present_benchmark` | 完整离线链接通过，业务源文件不变；没有执行 benchmark，未产生 FPS 数据 |
| P4 `gsp_showcase` | 原业务源文件在缓存 GSP 1.9.0 下完整链接通过；原 manifest 声明 `1.5.*`，**不证明原 1.5 组合兼容** |

Host 新测试覆盖 GRAM 早到/延迟完成、跨控制任务查询及首次 stop/delete、held lease 关闭保护、过期 lease ID、封帧队列停滞诊断、scroll 全绘协商、rotation/generation、tracking fence 后两个原始 VSYNC、取消观察不取消 DMA、取消后新帧不继承旧确认阈值。六种 FB 配置在模拟 8×4 屏逐像素对照；实际 LVGL 交接保持帧号连续，5 像素奇数宽 RGB565 验证 stride 不兼容时安全复制回退。

重现入口为 [prepare.py](prepare.py)。隔离副本关闭组件管理器，恢复原 manifest 提供的 GSP 顺序、LVGL 别名、hw_init 触摸/LCD 依赖；原工程、缓存和 IDF 源码不改。使用的缓存版本必须另核对，不能因关闭求解器而宣称原版本依赖成立。

本机 benchmark 首次 idf.py 在 CMake 完成后未退出；结束该父命令后，直接对生成目录执行 Ninja，最终构建成功。其余示例亦通过最终 Ninja 重新链接。IDF 对跨组件内部头文件路径仍有提示警告，未当成零告警验收。

全目录 `format --check` 仍报原有 `tests/test_fb.c` 的格式差异（该文件本轮未修改）；本轮新增/修改的兼容源文件均已按同一规则格式化。`git diff --check` 通过。未将全目录格式检查计为通过。

详细日志、源码/二进制/头文件 SHA256 留在本地 `artifacts/20261102/summary.json` 及相邻日志（git 忽略，不复制大二进制）。Host 不证明竞态穷举或真机 IRQ/缓存/光学时序。

## 尚未完成

C2 仍需补齐 TE/单 FB 等 C1 拒绝项、精确逐笔 push timeout（当前只有查询时封帧队列停滞诊断）、旧版非方形 90/270° 和 dirty 裁剪/对齐契约差分、实际 GSP 运行及其原发布版本组合。C3 三板默认配置实测、C4 常驻 coverage 位图收敛、C5 按证据决定的调度优化/默认切换仍待推进。
