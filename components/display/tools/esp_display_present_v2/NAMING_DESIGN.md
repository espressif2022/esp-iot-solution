# 组件命名与迁移盘点（N0 草案）

日期：2026-11-03。**状态：词法盘点和候选映射，尚未冻结，不可直接用于批量重命名。** 生命周期 T0–T2 后仍需按声明、链接、条件编译和消费者逐项复核。最新约定优先于 `TRANSITION_DESIGN.md` 第 11 节中较长的私有符号命名：`pv2_image_repaired` 应为 `present_image_mark_repaired`，保留私有。

## N1 实施记录（2026-11-03）

N1 符号迁移已应用；旧应用接口保持，原生 Presenter 创建入口是 `esp_display_present_engine_create`，其句柄是 `esp_display_present_engine_t`。`pv2_image_repaired` 已改为私有 `present_image_mark_repaired`。`NAMING_MAP.json` 的 `implementation.mapping` 是实际映射；原词法引用和候选名称作为历史盘点保留，不代表当前名称。

核心和 LVGL 分别提供独立的 `present_pv2_compat.h` 和 `present_lvgl_pv2_compat.h`，仅覆盖原公开扩展的源码别名，不提供旧二进制链接符号，也不导出私有控制、image 和 frame 接口。已增旧扩展与稳定 API 共存编译测试。符号迁移排除了 artifacts、evidence、deps、生成和构建目录。头的最终公开边界、组件目录合并、外部消费者调查和发布兼容期限仍待后续确认；旧盘点中的候选名、迁移步骤和状态不作为本次实施结果。

## 长期边界

交付 `esp_display_present` 和 `esp_lv_present` 两个组件。前者含应用门面、平台装配和单一核心；后者含现有 LVGL 门面与 binding。旧 `esp_display_presenter_*`、`esp_lv_present_*` 类型、符号与行为不变。高级扩展经审核才以 `esp_display_present_<职责>_*` 导出；跨文件私有 `present_<模块>_*`，文件内静态辅助函数用短动词。头文件可见性由 CMake 导出及长期契约共同决定，目录名或 `esp_` 前缀本身不能构成公开承诺。

原生 Presenter 和旧应用 Presenter 的 create 具有不同配置与句柄。候选扩展为 `esp_display_present_engine_presenter_create` 与 `esp_display_present_engine_presenter_t`，避免覆盖原有 `esp_display_presenter_create` 和 `esp_display_presenter_t`。其他原生 Presenter 方法沿用同一 engine 前缀，仍调用同一核心；不可强转两类句柄或绕过门面会话排他。计划创建 `pv2_presenter_create_planned` 同样归 engine 命名。候选 LVGL 原生绑定使用 `esp_lv_present_native_*`，以区分旧 LVGL API；是否公开仍需核实独立集成需求，内部绑定不因现有安装目录自动成为正式公共契约。

## 范围与导出

当前核心 CMake 的 `INCLUDE_DIRS include` 导出 `present_backend.h`、`present_executor.h`、`present_lcd_scanout.h`、`present_lcd_spi.h`、`present_output_caps.h`、`present_pixel_accel.h`、`present_pixel_engine.h`、`present_plan.h`、`present_presenter.h`、`present_te_timing.h` 共 10 个头；binding 的 `include/present_lvgl.h` 也导出。每个均需按接口职责、集成方需求与文档承诺决定是 advanced 公开头还是私有头，并同步检查被公开头间接引用的类型。建议经过审定的头归 `esp_display_present/include/advanced/`，前提是明确构建 include 路径；路径和文件名在逐项审核后冻结。LVGL 的已审定 native 头归 `esp_lv_present`，其余归 `src`。

核心 `src` 内有 `present_allocation.h`、`present_config.h`、`present_coverage.h`、`present_frame_core.h`、`present_image.h`、`present_limits.h`、`present_memory.h`、`present_os.h`、`present_records.h`、`present_transaction.h`、`presenter_control.h`、`backend/te_composer.h`、`platform/executor_hw.h` 和 `platform/lcd_scanout_internal.h`：均作为私有头处理，不能随合并进入公开 include。门面当前通过 `idf_component_get_property` 明确引用核心 `src`；合并后应在同一组件私有路径解决，LVGL 到门面的受控桥仍须保持私有并检查组件依赖方向。

## 消费证据与兼容策略

仓库生产消费者：`esp_display_present/src/facade.c`、`resources.c`、`resources.h`、`legacy_internal.h`、`present_te.c` 和 `esp_lv_present/src/legacy_lvgl.c` 使用 `pv2_*`。`resources.c` 使用 `pv2_presenter_create_planned`，旧 LVGL 门面调用 `pv2_lvgl_create*`。这些是同仓消费者，不证明第三方使用。V2 自有 host 测试、smoke 和 scanout_boundary 测试应用也使用这些接口；其他工作树、组件仓库、历史发布和预编译依赖不在此次只读扫描范围内，无法证明不存在外部消费者或 ABI 需求。

当前 `esp_display_present` manifest 和 CMake 依赖 `esp_display_present_v2`；`esp_lv_present` manifest 和 CMake 依赖 `esp_lv_present_v2`，后者 manifest 又指向核心。合并时同时更新这四处及新 CMake 的源码、public/private include、依赖，验证 Component Manager 打包后的路径、单独组件获取和自动发现；不能只改目录名。版本化组件名称是否已有已发布消费者尚未证实，发布与废弃版本须由实际发布记录和消费者调查决定。不要在未知情况下承诺立即删除旧组件依赖。

旧应用公开源码和二进制符号保持；V2 旧扩展在完成外部调查前不得假定可删除。源码兼容可通过独立兼容头和类型别名／inline 转发；若需兼容已编译二进制，则保留旧外部链接函数的薄包装，并验证导出符号，宏不能提供二进制兼容。兼容桥只转发至同一实例，不复制状态；逐项给出支持版本和最后移除版本后再实施。私有函数没有兼容承诺。兼容头不可把 frame、image、控制桥变成公共接口。

## 机读清单的含义和缺口

`NAMING_MAP.json` 扫描 V2 和两个门面的 C/H 文本，逐符号列出候选名及全部扫描到的文件／行引用。共 412 个不同的 `pv2_`／`PV2_` token，其中 233 个出现在现有公开头（**公开候选**），179 个为其余候选；两个数量包括注释、宏、类型、tag、条件编译名字及测试桩，并非已审核 API 数量。名字映射无候选目标重复；与两个门面的 C/H 现存标识符进行词法碰撞检查，结果为零。这个检查没有覆盖所有工具链生成标识符、其他组件、结构字段、include guard、预处理变体或 C/C++ 链接与 ABI；不能据此宣称最终无冲突。

分类按是否出现在公开头的词法出现推断，不是声明/定义或语义分类；局部静态名字也可能被建议成模块前缀。清单没有完整的声明、定义、验证用例和对应头迁移决定，不能满足 N0 的逐项完成线。下一步应用编译数据库与 AST/预处理结果识别符号类别、静态链接、声明和定义，删除仅出现在注释的伪符号，逐个确定头导出、职责动词、消费者及兼容方式，并在全仓检查目标标识符。明确公共例外和每个兼容桥对应测试后再冻结映射；N1 放在 T2 后独立实施。

## 验收

保留旧应用 C/C++ 头与链接验证，增加公开扩展的独立创建／渲染／销毁、所有权和受控 handoff 验证；核查新组件的公开与私有 include、两个入口是否共享同一核心，以及外部链接符号和已决定保留的旧符号。执行 Host、真实 LVGL、ASan/UBSan、离线 S3/P4/S31 构建与 GSP 1.5.1 链接；三板验证依最终候选单独记录。名称迁移不更改布局、buffer 所有权、执行路径或 tracking 时序。
