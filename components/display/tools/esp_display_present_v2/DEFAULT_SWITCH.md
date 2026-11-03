> 当前执行（2026-11-03）：生命周期、符号与私有导出收敛已落地，核心／binding 已并入 `esp_display_present`／`esp_lv_present`；Host、三目标离线与 GSP 链接通过，最终记录见新组件 MIGRATION_STATUS.md。旧目录仅用于历史记录，不再是生产组件。详见 [执行记录](TRANSITION_DESIGN.md#13-执行记录两组件候选2026-11-03)。未上板、未发布、未提交。

# V2 默认入口切换（2026-11-03）

## 唯一维护结构

| 组件 | 职责 |
| --- | --- |
| `esp_display_present` | 原组件路径与旧公开 API；V2 门面、资源装配、唯一测量式 TE 叶模块、公开 geometry |
| `esp_lv_present` | 旧 LVGL API，委托 V2 binding |
| `esp_display_present_v2` | 帧核心、图像构建、执行器、GRAM/TE/FB 输出策略 |
| `esp_lv_present_v2` | 原生 LVGL binding |

原 `compat/` 可选副本与 V1 生产源、私有头已移除。四份 presenter 公开头、LVGL 公开头保持字节不变。没有重写核心、增加模式或调整性能调度。

旧组件 manifest 使用本地 path 引入核心／binding；消费者不需要手动补 V2 的目录。禁用 Component Manager 的离线脚本显式加入当前组件，已去掉假装还能选择 V1 的 PROBE_IMPL 开关。历史源码使用 `git show 41b0c9b9b:components/display/tools/esp_display_present/...` 追溯；历史测试日志不改写。

## 切换范围

P4、S3 六种 FB 模式；S31 GRAM，以及已有实板证据的 TE 单 compose 0°、双 compose 90°。GSP 1.5.1 保持链接。历史性能 P4 28→27、S3 19→17、S31 25→25 FPS 不作为此切换门槛，也不是本轮重新测得的数据。

继续拒绝 S3 连续无 bounce 多 FB、refresh-on-demand 和驱动数量／格式／容量矛盾等配置。不放宽缓冲退休条件，不把未知驱动配置当作兼容成功。

## 本轮验证

- Host＋真实 LVGL：35/35；覆盖默认目录中的门面和 TE 叶模块。
- ASan/UBSan/LSan：35/35（detect_leaks=1）。
- P4／S3／S31：默认 manifest 自动依赖构建和链接通过。
- GSP 1.5.1：P4 showcase 与 GSP/LVGL handoff 离线构建、链接通过；link map 的 archive 路径经 symlink 解析确认是缓存 1.5.1。
- 默认公开 API Unity smoke：S3 离线构建通过（未上板运行）。
- 公开头文件与切换前逐字节一致；生产构建无 compat 副本、V1 私有头和旧流水线源。
- 独立 consumer 只依赖旧组件，通过 manifest 自动发现核心；不依赖原 V1 私有目录。
- `esp_display_present/test_apps` 更新为公开 API smoke；原 V1 私有驱动模型由 V2 Host 的生命周期、扫描、旋转、repair 与真实 LVGL 回归接替。

本轮未烧录，不宣称新候选版本通过了实板 TE 时序或完整默认切换板测。发布前仍应在原三块板对切换候选做短回归。IDF IPC 本身仍无硬超时保证。

## 后续维护

新行为只进入核心，门面保留旧类型／返回值与资源装配。TE 测量只在默认组件中维护一份。若继续优化 S3，先分别测门面查询、Presenter 锁、copy、repair 和 cache；不为 P4 1 FPS 差距增加调度层。

### 复现入口

```sh
python3 tools/dev.py test --with-lvgl
ASAN_OPTIONS=detect_leaks=1 python3 tools/dev.py test --with-lvgl --sanitize
```

在核心目录执行以上命令。默认依赖发现使用 `test_apps/legacy_compat/compile`（Component Manager 开启，仅本地 path 依赖）；GSP 使用 `test_apps/legacy_compat/prepare.py --gsp-package <本地1.5.1目录>`。编译记录核对 `esp_display_present` 的源文件集合仅为 facade/resources/present_te/present_geometry；handoff 使用默认 `esp_lv_present/src/legacy_lvgl.c`。

## 后续内部交接

默认目录切换完成后的状态收敛设计见 [TRANSITION_DESIGN.md](TRANSITION_DESIGN.md)。该计划尚未实施；保留旧 tracking 的额外两个原始 VSYNC 观察语义。

## 后续正式命名收敛

按 [交接设计第 11 节](TRANSITION_DESIGN.md#11-正式接口分层与去版本化命名) 的 N0／N1 独立实施。旧应用 `esp_display_presenter_*` 保持不变，扩展与内部 `pv2_*` 去版本化并明确公开／私有边界；内部符号不因使用 `esp_*` 而公开。组件目录和包依赖名另行整理，本轮仅落设计，尚未改名。
