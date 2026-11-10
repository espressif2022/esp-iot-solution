# S3 RGB XIP OFF 单角度模式判定（2026-11-10）

[判定表](comparison.html) · [CSV](comparison.csv) · [逐项数据与来源](matrix-data.json)

按用户最新要求仅0°，复用已有完整结果，只补缺失模式。11个支持组合，每项完整16场景，共176个选定场景；adapter AUTO记−，NONE不纳入。原较大序列中额外角度原始记录不删除，但不增加本页验证范围，也没有继续补跑其他角度。

- present：6个模式已验证，6个模式运行期触发；DOUBLE_FULL, TRIPLE_FULL, DOUBLE_DIRECT, DOUBLE_PARTIAL, TRIPLE_PARTIAL, AUTO。
- adapter：5个模式已验证，5个模式运行期触发；DOUBLE_FULL, TRIPLE_FULL, DOUBLE_DIRECT, DOUBLE_PARTIAL, TRIPLE_PARTIAL。

固定原16MHz PCLK、十行bounce、240MHz CPU、80MHz PSRAM、XIP OFF、单输入tile。只读驱动观察补数EOF不足和既有自动重启，不修改控制逻辑。运行期门控排除跨入创建阶段的首半帧，窗口包含benchmark结束回调；创建/关闭等窗口外异常另外列。Host ASan/UBSan验证门控、首半帧、重置和错误panel过滤。

present六项0°来自已完整结束的既有序列。adapter DOUBLE_FULL 0°也已完整结束，其后的其他角度因用户缩减范围被中止；旧序列未整体跑完在来源元数据中如实标注，复用的是已完整关闭并PASS的那一项16场景。其余四模式单角度补测正常结束。没有将被中断的场景当成有效成绩。

结论仅限这块S3 R16/GC9503及该配置的benchmark负载，不外推所有S3硬件或所有负载。零计数只能说本轮未触发。FPS受诊断开销影响，不替换旧正式性能表。生产组件和基础IDF不改；补数时限325us、此前最长实测694us的归因详见相邻s3-rgb-supply-20261110。

按用户要求不恢复原固件。结束后保留已测XIP ON/原16MHz诊断参考固件，状态见controller.json。归档不含build、依赖缓存、固件或flash备份。
