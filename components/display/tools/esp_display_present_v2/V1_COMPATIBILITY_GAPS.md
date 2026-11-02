# V1 成功路径兼容状态

更新：2026-11-02。以下区分代码实现、Host 和实板证据。默认示例仍链接原组件，尚未整体切换。

| 路径 | 当前实现及证据 | 仍有边界 |
| --- | --- | --- |
| void cancel_frame 与异步访问 | 对外撤销 frame/lease，内部保留源，后续准入排空；Host 通过 | P4/S3 新固件取消后重绘通过；不允许提前释放在途源 |
| 非方形运行时跨轴旋转 | 门面固定逻辑尺寸，更新物理布局；PARTIAL/FULL/DIRECT Host 像素对照通过 | 原生 V2 默认尺寸语义不变；不是任意修改面板扫描时序 |
| TE_SYNC | 借用 V1 测量策略，通过注入 gate 接原生 TE；GPIO/定时器停止和 join 可重试；Host 通过 | S31 经 SPI 只读事实确认直传后，单 compose 0°/双 compose 90°各 12 帧通过；未知路径仍 staging/超窗暂停，不借历史 native TE 代替 |
| RGB/DSI MODE_NONE | 单图像、持久 DIRECT；明确允许扫描中写入；Host、P4/S3 功能测试通过 | 单 FB 本身可能撕裂；不能套用多 FB 的退休含义 |
| RGB 自动能力识别 | 新增公开只读 IDF 查询，旧应用调用不变；Host 和 SDK 编译通过 | 需要本地 SDK 补丁；未修补 SDK 仍用显式确认。见 driver_patches |
| SPI PSRAM drawbuf | GRAM 使用内部 DMA 转换池；TE 使用有界内部 DMA staging；Host 延迟 DMA/超时测试通过 | 确认 direct-PSRAM 的 IO 不再重复 staging；未知配置继续保守 staging，仍须窗口准入 |
| FB swap / 非 64B 容量 | 放开创建；借用有效容量不虚增；硬件 probe 不满足时执行前选 CPU；Host 通过 | 仍禁止越界 cache 维护；不承诺每个布局硬件加速 |
| 双核 IPC | 门面构建默认选择 IPC，保留实际回调退出屏障 | 不删除 join 或伪造退出 |
| P4 GSP/LVGL 黑屏反馈 | 最新纯色探针及后续 GSP/LVGL 用户均确认可见，日志多轮交接无错误 | 使用 GPIO26 背光及 1000 Mbps 板级参数；不把此前纯日志 PASS 当视觉通过 |
| GSP 原 1.5 发布组合 | 已找到并实际链接缓存 1.5.1；离线构建通过 | 实际原 V1 基线通过；修复 128 脏区容量后 V2 启动四场景且用户确认画面正常；缓存 1.9 不作替代 |

## 仍拒绝的驱动域

- S3 连续 RGB、无 bounce、多 FB：现有 EOF 不能证明预读已经结束，未获得可靠退休事件。
- refresh-on-demand 扫描：尚未完成真实驱动生命周期适配。
- 驱动池数量/格式/有效容量矛盾、外部变换、未知自定义 bounce 所有权。
- 0° 原地字节交换 GRAM 实例运行中改非零旋转：没有预建转换池，仍明确拒绝。

V1 配置校验通过不等于旧完整路径实际成功。尚未证实的路径不得标成兼容通过。新门面不因追求超集而取消缓冲安全条件。

## 本轮验证

完整 Host/LVGL：34/34。ASan/UBSan/LSan：34/34，`detect_leaks=1`。P4/S3/S31 的 V1、V2 六份固件均完成离线构建。首轮对照程序误传空 commit，V1 拒绝；已改成明确 FULL 覆盖，首轮不作性能比较。最新结果及修复过程见 [本轮验收](test_apps/legacy_compat/RESULTS_CLOSE_20261102.md)。
