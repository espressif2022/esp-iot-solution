# V2 八项代码收敛与验收计划

本轮按用户要求完成前八项代码工作，再统一 review。代码阶段完成本地 Host 与离线编译；随后已启动 S3/P4 独立测试工程上板验收，首轮 DOUBLE_FULL 通过，详见下文。V1 与现有产品工程不变。本文为当前状态，早期分阶段记录不代表现状。

| 项 | 交付 | 当前结果与限制 |
| --- | --- | --- |
| 1 | TE／FB 非阻塞 repair、prefill | 异步 ticket 驱动，service 每次只推进已完成阶段；取消／stop 超时保留源和目标；同步默认路径保留 |
| 2 | 运行时旋转与触摸逆变换 | GRAM／TE／FB 四方向、非方形画面；Presenter drain 后切换，超时不改配置；FB 保留物理扫描池，另建逻辑绘制池 |
| 3 | Presenter 显式 receipt／tracking | commit/query/wait/release/cancel_tracking；32 个观察记录；取消观察不取消传输；不冒充 V1 generation／VSYNC 协议 |
| 4 | P4 DSI scanout 与 PSRAM/cache 边界 | 真实 IDF adapter、驱动 FB 池、FB0 初始扫描保留、两次提交后帧边界确认、C2M 与安全停止；真实 PSRAM 一致性待上板 |
| 5 | RGB scanout | 复用同一 adapter 的 RGB bounce 连续刷新路径；明确拒绝非 bounce 模式，后者缺少当前平台统一可验证的退役语义 |
| 6 | DMA2D／PPA executor | 独立执行器、串行共享、完成后归还借用、cache line／MSPI 约束、提交前 CPU 回退；DMA2D 用本地 IDF 6.2 私有 API，升级需复核 |
| 7 | LVGL 零拷贝 | 可选 FULL／RGB565/native／兼容 stride／多 FB；RENDER_START 先 acquire，flush_ready 前解绑；保留失败准入的私有安全缓冲。PARTIAL、DIRECT、RGB888 保持复制路径 |
| 8 | 统一 review、回归与文档 | 普通 Host、ASan/UBSan、P4 编译；说明 API、资源代价、支持边界与后续硬件验收，不将模拟结果当成无撕裂证明 |

上述第 4～7 项提供了明确限定的实现，不表示支持所有 IDF 版本、RGB 扫描模式或所有 LVGL 零拷贝组合。未承诺旧 API 二进制兼容。

## 本地验收

- 17 组核心：records、coverage、repair、accel、scanout、rotation、pixel、gram、lcd_spi、presenter、te_composer、te、presenter_te、fb、event、scheduler、headers_cpp。
- 4 组真实 LVGL 9.5：lvgl_zero、lvgl、lvgl_te、lvgl_fb；合计 21 组。
- repair 用条件变量阻塞执行器，检查 service 不阻塞、候选帧不提前发送、prefill 未完成不借出、取消／stop 保留和重试。
- rotation 覆盖初始与运行时四方向、三个后端、RGB565／RGB888、同步／异步、外部扫描缓冲与逆坐标；独立像素期望值对照。
- accel 编译生产适配器，驱动 stub 独立解释描述符；验证像素、旋转方向、字节序、cache 对齐、MSPI 限制、早到完成、错误后不重复 CPU 拷贝及销毁失败重试。
- scanout 验证初始扫描 FB 不可写、提交期间边沿不冒充确认、两边界确认、不确定接受隔离和 stop 重试。
- LVGL 零拷贝验证实际 lease 地址、完整像素、pending／scanning 源不被重写、无脏区不申请 lease、超时暂停、销毁 binding 后传输仍可完成。
- repair／rotation／scanout／accel／lvgl_zero／presenter 六组各重复 100 次通过。P4 + LVGL 与 ESP32-S3 核心 smoke 均完成离线编译链接。
- ASan/UBSan 使用 `detect_leaks=0`；当前环境未验收泄漏检测、TSAN、竞态穷举或真机时序。

```sh
python3 tools/dev.py test --with-lvgl
ASAN_OPTIONS=detect_leaks=0 python3 tools/dev.py test --with-lvgl --sanitize
# 激活本地 ESP-IDF 后，只编译，不烧录
python3 tools/dev.py idf-build --target esp32p4 --with-lvgl
```

## Review 顺序

1. `include/present_backend.h`、`present_presenter.h`：lease、receipt、旋转和 stop 契约。
2. `src/backend/te_composer.c`、`fb.c`：repair/prefill 与长期扫描借用。
3. `src/platform/lcd_scanout.c`：事件到退役证据的映射，驱动版本假设。
4. `src/platform/pixel_accel.c`：硬件准入、cache、失败和完成屏障。
5. `bindings/esp_lv_present_v2/present_lvgl.c`：绘制前取得写权限，错误路径解绑。
6. `review.md` 的已处理建议与保留项，结合上述生产源码测试阅读。

## 统一上板阶段（已启动）

已核对并完整备份原 Flash：S3 `/dev/ttyUSB0`，MAC `90:e5:b1:d0:a9:d8`，R16、480×480 GC9503 RGB565；P4 `/dev/ttyACM0`，MAC `e8:f6:0a:e7:a7:8d`，芯片 v3.2、1024×600 EK79007 MIPI RGB565。PSRAM 分别 16/32 MB。

2026-10-30：独立工程 [test_apps/boards](test_apps/boards/README.md) 完成两板编译、烧录和 CPU DOUBLE_FULL 首轮验收。两板日志均覆盖 frame 0..240，PRESENT_CONFIRMED 成功、pending=0、sticky=ESP_OK，采样内部 heap 保持不变；用户确认画面正常。保留两次边界确认，没有降低阈值。详见 [RESULTS.md](test_apps/boards/RESULTS.md)。

第二轮五种模式首轮硬件验收已通过：S3 3 轮、P4 2 轮，整帧像素一致，stop/recreate 与 heap 检查通过，用户确认画面正常。三缓冲目前仅验证配置和生命周期。后续：三缓冲实际第三缓冲使用/排队背压 → LVGL → 旋转/触摸 → DMA2D/PPA 对照 → 长时间压力。尚未验收的模式不因 FULL 通过而判定通过；两板结果不能替代 SPI/TE 实屏时序验收。

## 独立后续项

非 bounce RGB 的平台专属退役协议；更多 LVGL 零拷贝组合；V1 generation/VSYNC 兼容层；裸后端及 SPI 等待的全面事件化；减少 DIRECT 防御性 prefill+repair 写放大；硬件性能调优。它们不混入本轮已通过的能力声明。


## Review 修改验收（2026-10-30）

已落实：目标行带 cache 同步、coverage 字节批处理、旋转回零释放逻辑池；同步修改资源契约与命名风格。新增 coverage 组，普通 Host 与 ASan/UBSan 均为 20/20（detect_leaks=0）；P4 + LVGL、S3 核心离线编译通过。Doxygen XML 生成零警告；coverage、rotation、accel、repair 四组各重复 100 次通过。旋转测试覆盖像素池内存水位恢复；cache 测试核对准确的同步区间和边界。

DSI 两次边界规则保持不变。本地驱动源码显示 DMA 完成后重启持续扫描；正式上板先做单次提交后持续边界计数，不通过降低阈值绕过异常。用户确认：P4 ACM0 为 1024×600 EK79007 配置，S3 USB0 为 480×480 GC9503CV 配置；该代码 review 阶段未上板；后续首轮上板结果见上文。

## 公共记录规则收敛（2026-10-30）

新增私有 `src/present_records.h`，三个后端共享以下规则：

- `pv2_record_t`：token、external、terminal、首次错误；TE/FB 直接使用，GRAM 以首成员组合并保留 pending/leased/sealed/canceled。
- 按实际记录 stride 查找 token；是否要求 external 由调用点指定，不跨实例共享池。
- 统一公共状态输出、首次 record error 与 sticky fault 保留、最近一次完成取消的幂等记录。
- 统一防溢出的矩形/脏区校验及 stop 剩余超时预算。

模块不持有锁，不调用驱动，不释放像素，不选择完成时机，不统一后端状态机。
调用者继续持有所属后端锁；ISR 会调用的错误/取消发布函数保留 ISR 属性。
GRAM 的 terminal 仍由 sealed/pending 推导；TE 的传输终态不冒充显示确认；FB
只在成功确认时声明 present_confirmed。池大小、空槽分配、回收与缓冲所有权仍
由后端管理，避免引入第二个资源状态机。当前没有新增时间戳字段；未来新增时
需先定义事件语义，再扩展公共记录/输出入口以及各后端真实完成点。

新增 `records` 测试包含组合记录 stride、零/旧 token、external 限制、首次错误、
失败终态与显示确认区别、矩形溢出、超时边界，以及同一套 GRAM/TE/FB 契约检查：
非法 begin 不产生 token、重复取消成功、旧取消 token 失效、超过池容量次数的
begin/cancel 循环正常复用、取消后 receipt 不可查询且不污染 fault。

验收：普通 Host + LVGL 21/21；ASan/UBSan 21/21（detect_leaks=0，未做泄漏检测）；
S3 核心与 P4 + LVGL smoke 离线编译通过。本轮未连接或烧录设备，之前板测结果
对应重构前固件，不能当成本次修改的板上回归结果。
