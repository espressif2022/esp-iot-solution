# 模式解析规则收口 · 2026-11-06

基于 `ccc334c16`。新增私有present_mode模块，集中显式FB模式的合同/缓冲数量表、公共target映射，以及current/legacy AUTO默认策略。模式策略类型归该模块，Plan引用它，避免模式模块反向依赖完整Plan。

## 保留的语义

- 公共FB NONE：DIRECT持久画布，通过DEFAULT进入engine，保留active_writes。
- 底层FB NONE：PARTITION、单FB。current AUTO由给定buffer_count选择，legacy/public AUTO在FB上选择TRIPLE_PARTIAL。
- GPIO0有效；TE开关与负GPIO共同决定公共TE能力。底层current AUTO仍要求window/budget条件，不因GPIO能力而虚构timing。
- 所需framebuffer查询与Resources使用同一target策略；查询返回的是driver FB数量，TE compose与draw tile数量仍独立。
- Resources保留硬件能力、内存选择与tile默认数量，Plan保留布局、预算和可创建性验证。独立创建入口仍防御性重验同一规则，不能信任外部修改过的plan。
- 公共头文件、缓冲交接、超时和扫描确认机制未改。具名preset尚未解析前的资源配置表示也保持原样。

## 配置等价性

改动前后同一Host工具链与ASan/UBSan，使用snapshot-probe.c.gz中的独立探针，对序列化结果逐字节比较：

- 公共入口55,296组：四类panel、八种模式、四角度、三种TE状态、0至3个显式FB、0至2个tile/compose配置、RGB565/RGB888、IO存在与否。
- 底层plan入口6,912组：三个backend、九种mode含DEFAULT/AUTO、1至4个buffer、四角度、外部buffer存在与否、TE能力、两种defaults及timing条件。
- 含拒绝配置，共62,208组。记录公共数量查询返回码与结果、规划返回码、最终backend/mode/contract、各池数量/容量、布局尺寸、内存估算与关键执行标志。117,504行结果完全相同，SHA见snapshot-comparison.json；两个相同输出只存一份压缩快照。

该矩阵使用8×4非方形画布，不能代替全部尺寸、真实堆状态或硬件扫描验证。probe源码从组件目录编译，包含路径为src/include/dev/tests/dev/tests/support，链接Host的present_legacy、present_legacy_resources、present_engine库及pthread/m；baseline与candidate分别使用各自提交的库。

## 回归与编译

新增test_mode固定入口差异、GPIO0、外部数量约束与DEFAULT直通语义，C++ header smoke包含新模块。Host43/43通过；三芯片完整编译记录见validation.json和各板manifest/build.log.gz。本轮未烧录，无视觉或帧率结论，IDF保持干净。
