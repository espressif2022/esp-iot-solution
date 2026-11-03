# 门面超集：旋转扩展验收（2026-11-02）

取消为了复刻 V1 限制而设置的 GRAM／DIRECT 非零旋转拒绝；默认模式与池数量不改。查询、资源创建、resolved plan、运行时设置四处一起调整；没有给核心添加模式分支或改变后端生命周期。

测试覆盖：

- GRAM 0/90/180/270° 创建，buffer-count 查询一致；每个角度再同轴旋转 180° 后提交第二帧，两帧逐像素参考比较。
- FB PARTITION/FULL/DIRECT 非方形四方向，各自运行时同轴切换后再次逐像素比较；DIRECT 是本轮新增覆盖。
- 非方形运行时跨轴仍失败，generation 与逻辑尺寸不变；原地字节交换 GRAM 的运行时旋转仍由核心拒绝，generation 不变。
- V1 defaults 下 GRAM 与 DIRECT 的旋转规划不再被 profile 人为拒绝，转换池／逻辑池规划仍生效。

调度重复测试发现 last_service_error 的瞬时状态可能已被后续无操作 service 覆盖。修正该用例的持久性假设，仍要求明确拒收后 pending=1、无 sticky fault、新边沿重试成功；没有改变生产调度代码。

本轮不烧录，Host 结果不等于实板旋转、缓存和撕裂验收。TE 平台闭环、单 FB、RGB 无 bounce、非方形运行时跨轴仍未完成。原有 V1 成功路径应保持契约；门面尚未实现完整 V1 超集。

验收结果：Host/LVGL 34/34；ASan/UBSan/LSan 34/34（detect_leaks=1）；plan/scheduler/legacy_resources/legacy_facade 各重复 50 次共 200 次通过；S3/P4 离线构建通过；格式和补丁检查通过。日志及源码 SHA256 见本地忽略目录 artifacts/20261102-superset-rotation/summary.json，包含首次调度失败与修正后的结果，不隐藏失败记录。
