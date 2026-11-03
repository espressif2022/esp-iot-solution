# 显示组件合并与构建迁移审计

日期：2026-11-03。状态：设计；尚未移动文件、改动构建或执行本轮验收。本文补充 TRANSITION_DESIGN.md、DEFAULT_SWITCH.md，用户最新决定优先：交付 `esp_display_present` 与 `esp_lv_present` 两个组件，内部跨文件符号使用 `present_<模块>_*`。尤其 `pv2_image_repaired` 的目标为 `present_image_mark_repaired`，取代 TRANSITION_DESIGN 第 11 节较长的内部名称；公共 API 的命名仍按 N0 审定。

## 依赖现状与目标

当前 stable presenter 的四个源文件在 `esp_display_present`，通过 PRIV_REQUIRES 和 manifest path 依赖 `esp_display_present_v2`，并把其 `src` 私有路径加入自身编译。`esp_lv_present` 的 `legacy_lvgl.c` 依赖 stable presenter、V2 LVGL binding、V2 core 和 LVGL，并直接包含 stable presenter 的 `legacy_internal.h`。V2 binding 独立依赖 V2 core 和 LVGL。四个组件只对应一个执行引擎，但打包和依赖仍是四个组件。

目标构建图是 `application → esp_lv_present → esp_display_present → IDF drivers`，也允许 application 直接依赖 `esp_display_present`。`esp_display_present` 不依赖 LVGL、`esp_lv_present` 或其头；LVGL 两个实现源都属于 `esp_lv_present`。单个 IDF 组件内用私有目录分层不造成组件依赖环。源代码层需另查：core 不反向 include facade/platform 的旧类型、GPIO TE 或 LVGL；跨组件 renderer 会话继续走窄桥，不复制 Presenter 或绕过锁。

## 文件映射

以下路径都相对于 `components/display/tools/`。源、目标发生同名冲突时，不覆盖：先将旧 facade/platform 文件保持原名或改为明确用途，再逐一比对职责和编译列表。列出的目录规则覆盖目录下所有跟踪及未跟踪的相关项目文件；搬迁前应保存清单及散列，排除 build、缓存、生成物，不删除用户未提交内容。

| 当前 | 目标／处理 |
| --- | --- |
| `esp_display_present/{include,src,Kconfig,license.txt}` | 原地保留 stable API、facade/resources/geometry、测量式 TE；根据职责可整理子目录，不改变公开四头的既有签名和布局。 |
| `esp_display_present_v2/src/presenter.c` 和 `presenter_control.h` | `esp_display_present/src/presenter/`；控制桥保持私有，仅供已确认的内部调用。 |
| `esp_display_present_v2/src/present_image.*`、`present_coverage.h` | `esp_display_present/src/image/`。 |
| `esp_display_present_v2/src/present_frame_core.*`、`present_records.h`、`present_transaction.h` | `esp_display_present/src/frame/`；文件及符号名遵循 N0，目录仅表达职责。 |
| `esp_display_present_v2/src/backend/**` | `esp_display_present/src/backend/`；GRAM、TE、FB 与 composer 保留各自实现。与现有 `src/present_te.c` 的测量式 gate 职责不同，不合并或重复编译。 |
| `esp_display_present_v2/src/pixel/**` | `esp_display_present/src/pixel/`。 |
| `esp_display_present_v2/src/platform/**` | `esp_display_present/src/platform/`；包含 LCD、硬件 executor、pixel accelerator，保留目标平台条件编译。 |
| `esp_display_present_v2/src/present_{os,event,te_timing,config,plan}.c` 及 `present_{os,memory,limits,config,allocation}.h` | `esp_display_present/src/` 或按职责放入 `presenter/`、`platform/`；`present_te_timing.c` 是 timing 计算，与既有 `present_te.c` 测量 gate 并存。检查每个源恰好编译一次。 |
| `esp_display_present_v2/include/*.h` | N0 对 10 个头逐个确定正式公共扩展／私有归属。仅经审定的扩展进入 `esp_display_present/include/advanced/`，依赖闭包一起整理；其余进入 `src` 私有目录，不能机械全部导出。公共扩展头名及其互相 include 同步迁移，避免同名不同定义。 |
| `esp_display_present_v2/bindings/esp_lv_present_v2/present_lvgl.c` | `esp_lv_present/src/present_lvgl.c`，与 `legacy_lvgl.c` 一并编译。 |
| binding `include/present_lvgl.h` | N0 审定供集成方使用的 LVGL 扩展后放 `esp_lv_present/include/advanced/`，否则放私有目录；stable `esp_lv_present.h` 不变。不要把仅实现委托所需的声明默认升级成公共契约。 |
| V2 `tests/**`、Host CMake、`tools/dev.py` | 放入 `esp_display_present/tests/` 与 `tools/`，Host 构建入口独立为 `tests/CMakeLists.txt` 或等效位置，使用生产源与两个组件相对路径；真实 LVGL 测试编译 `esp_lv_present` 的 binding 和 legacy 源。保留 executor 四变体、链接 wrap、C++ header smoke、测试专用 shim、计时/超时和 sanitizer。 |
| V2 `test_apps/{smoke,boards,legacy_compat,scanout_boundary,driver_patches}/**` | 迁入 `esp_display_present/test_apps/` 对应子目录，先检查与现有 `test_apps/main`、根 CMake、README 名称冲突，保留现有 stable smoke；结果/历史证据保持原内容，修复活动文档链接。生成构建目录和 deps 缓存不作为源码迁移。 |
| V2 `ARCHITECTURE.md`、`FLOWS.md`、`OWNERSHIP_SEQUENCES.md`、设计与历史文档 | 放入 `esp_display_present/docs/` 或约定的迁移历史目录；转存历史记录，更新入口说明及相对链接，不把历史测试数字说成新结果。V2 专用 CMake 和 manifest 不作为第三组件保留。 |
| stable `README*.md`、`COMPATIBILITY.md`、`STATE_MACHINE.md`、`CHANGELOG.md` | 更新当前组件图、获取方式、路径与测试入口；保留标明日期的历史事实。stable LVGL 的 README、CHANGELOG 同步更新。 |

实际路径可按实现时文件职责微调，但要提供逐文件源→目标清单，避免 glob 误纳入多余源；迁移前后分别以 `rg --files` 与散列检查，确认所有源文件、测试夹具和证据都有归宿。V2 根遗留物如果只是构建产物/用户其他未跟踪文件，不擅自删除。

## CMake 与 manifest

1. 合并两个 presenter 的显式 SRCS；`INCLUDE_DIRS` 为 stable include 及经过 N0 审定的 advanced 公共目录，`PRIV_INCLUDE_DIRS` 含各内部子目录。IDF 公共/私有依赖按公共头实际 include 关系合并：现有 `esp_common`、`esp_lcd` 公共需求及 `freertos`、`esp_timer`、`heap`、`esp_mm`、`esp_hw_support`、`esp_driver_ppa`、`esp_driver_dma`、`esp_system`、`esp_driver_gpio` 私有需求逐项核对目标及 IDF 支持性。移除 presenter 对 `esp_display_present_v2` 的 PRIV_REQUIRES、跨组件 `idf_component_get_property`、manifest path。保留 `c_std_11` 和平台条件。避免通过对方私有 src 暴露公开头。
2. `esp_lv_present` 注册 `legacy_lvgl.c` 与迁入的 `present_lvgl.c`，公共依赖按公开头要求包含 presenter、LVGL，私有依赖包含 timer；删除两个 V2 REQUIRES 与 manifest path。保留现有 LVGL 源目录/registry 名称适配及 `lv_freertos.c` 编译选项，兼容 Component Manager 本地与 registry 名。检查公开传递依赖符合最终头的 include 闭包。
3. 当前 `legacy_lvgl.c` 直接 include `esp_display_present/src/legacy_internal.h`，因此合并 binding 不会自动消除跨组件私有访问。保留方向 `LVGL → display`；评估将必要的 renderer 会话声明提取为窄的内部桥头，并通过显式受限私有 include 路径消费，接口不得变成公开应用 API，也不得加入反向组件依赖。不得绕开 producer handoff 或新造第二生命周期。
4. 检查 manifest `repository_info.path`、examples、排除规则与组件包产物；稳定组件不再要求相邻 V2 path，打包验证不能遗漏私有源、扩展头、license；避免把大型测试证据/构建产物打包进 registry。版本号和发布兼容策略需要单独确定，不将源码迁移视作发布。检查已发布组件的消费者及 registry 求解：`esp_lv_present` 对 presenter 的版本约束必须与未来发布版本匹配，不能只靠本地 override_path 证明发布可用。
5. 同步 `tools/dev.py` 的 ROOT、format 目录与 LVGL 寻址、Host CMake 的 add_subdirectory/target、源路径、linker `--wrap` 在 N1 重命名后对应符号；检查独立 executor 变体不因统一库遮蔽禁用驱动链接错误。迁移后不把旧 Host 对 V1+V2 并行链接的注释当作现状。
6. 更新 `test_apps/smoke`、`boards`、`scanout_boundary` 的 EXTRA_COMPONENT_DIRS、COMPONENTS、REQUIRES；`legacy_compat/prepare.py`、`prepare_board.py`、`scanout_boundary/prepare.py` 会生成 overlay/工程，同步所有脚本中的路径及源清单；`legacy_compat/compile/main/idf_component.yml` 保留只引用 stable 组件的依赖发现测试。示例和仓库其它消费者用全仓 `rg` 搜索组件名、旧 include、源码路径、CMake target、脚本生成文本，不只修 CMake。历史 evidence 可以保留 V2 字样但不应作为活动构建输入。

## 执行阶段与验收

按 TRANSITION_DESIGN 的 T0→T1→T2 生命周期收敛先完成并单独验收；N0 完成公共/私有符号与头映射，N1 独立实施去版本化和冲突检查。组件搬迁在这些完成后单独进行，避免把行为修复混入文件移动；本表是设计而非完成证明。任一步前记录已有变更，不能重置用户工作树。

1. 建立基线：归档现有测试结果、稳定五个公开头与对外应用符号及类型布局、V2 符号/头及全部消费者清单。逐个审定扩展兼容：对公开过的 `pv2_*` 记录外部消费者、兼容桥或明确迁移期限；不盲目承诺 ABI 或移除调用。核对扩展 create 与旧 presenter create 签名冲突。验证源包中无多重同名组件。
2. 按文件映射迁移、改 CMake/manifest/脚本与活动文档。检查编译数据库/链接 map 中每个核心源恰好出现一次，`nm` 检查重复定义及未解析符号；`rg` 检查活动依赖不含旧组件名和旧公共头路径。注释/历史证据单独归类，不机械替换历史。
3. 从干净的独立 build 目录，使用本地真实 LVGL 源运行 Host 普通及 ASan/UBSan（`ASAN_OPTIONS=detect_leaks=1`），列出测试数、命令、LVGL 和编译器版本；包含 legacy facade、TE gate、LVGL handoff、executor 四组合、header smoke；无 LVGL 的最小 core 构建也通过。不得以旧 35/35 替代新结果。
4. 原 stable 应用公开头逐字节比较或逐项解释经批准的变化；独立 consumer 仅声明 `esp_display_present`，另外的 LVGL consumer 仅声明 `esp_lv_present`；分别通过 Component Manager 本地依赖发现与禁用 Component Manager + 显式仅两组件路径方式构建。使用网络隔离及已缓存依赖证明离线；记录 IDF 版本、target、配置与完整日志。检查头可从 C 和 C++ 编译，私有头不被导出；核对链接图无 V2 component 及无重复引擎。
5. P4、S3、S31 按现有 compile/smoke/boards 构建目标在新路径重新离线构建；验证 `esp_display_present`、`esp_lv_present` 自带的旧 API Unity smoke 编译。验证 P4 GSP showcase 和 GSP/LVGL handoff 使用本地 GSP **1.5.1**：检查 package manifest、archive 实际路径（解析 symlink）、SHA256 与 link map；源码业务文件散列与原示例一致，记录工具脚本生成的依赖改写。其他版本只能证明对应版本，不代表 1.5.1。
6. 跑源码打包/隔离消费检查，确认不引用仓库旁边的 V2 目录；对外能力及支持/拒绝矩阵保持既定边界。T4 三板短回归需重新确认设备身份、记录视觉和日志，本设计不声称本轮已实板运行。

## 主要风险与处理

- `advanced` 下的传递 include 会泄漏内部类型或引用未安装头：以每个对外头的独立编译、安装包目录和依赖闭包测试确定范围，必要时调整契约，而非直接导出所有 V2 头。
- LVGL 的私有桥依赖既有结构，直接删 `legacy_internal.h` 搜索路径会破坏编译；任意将其变公共又会扩大契约。用方向明确的窄桥并检查核心无 LVGL 引用。
- 两份 TE 均属于唯一引擎但职责不同；误去重导致测量 gate 或 timing 消失，误同时复制导致重复定义。按源/符号表核对。
- Host 测试多数直接引用旧目录和 `pv2_*` linker wrap；仅修 IDF 构建会产生假通过或丢失覆盖。分别核验 Host、IDF、Component Manager 和离线脚本。
- 已有单组件版本约束、`override_path` 和外部缓存可能掩盖生产包缺文件或错误版本；隔离包验证、解析 link map，并区分本地源码通过与实际 registry 发布。
- 重命名与生命周期修复存在顺序依赖；保留逐阶段审查和当前 API 契约，不借合并调整支持范围、线程或性能策略。
