# ESP Display Present

[English](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README.md)

`esp_display_present` 为 ESP LCD 面板管理帧提交、缓冲所有权和显示同步，以同一组渲染接口接入 RGB/MIPI 扫描输出与 Panel IO 传输。应用负责初始化面板和总线，Present 负责经它提交的像素工作的生命周期。

LVGL 9 通过 [esp_lv_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md) 接入；自定义渲染器使用 [公开 Presenter API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h)。

## 接入要求

- ESP-IDF 6.0 及以上；输入为 RGB565 或 RGB888，须与面板配置匹配。
- 面板已初始化。RGB/MIPI 至少需要两张驱动 framebuffer，且驱动须支持可确认的缓冲交接。DSI 还要求 DPI 提供帧完成与 VSYNC 回调，ESP-IDF 6.0 不具备这些回调。
- Panel IO 提供 panel 和 IO 句柄。TE 还需要接入并启用 TE 信号，且总线能在允许窗口内发送合成帧。合成帧在 PSRAM 且输出端未开启直 DMA 时，Present 使用两块有界内部 DMA 中转缓冲分片发送；内部 DMA 合成帧或已确认开启 PSRAM 直 DMA 时直接提交。驱动无法查询直 DMA 能力时，`hw.spi_psram_dma_direct` 须与实际 IO 创建标志一致。

ESP32-S3 RGB 须在面板驱动中启用 bounce buffer。RGB 使用连续刷新和未经驱动变换的坐标，逻辑旋转交给 Present。SDK／BSP 须提供实际 framebuffer 配置事实；较旧驱动缺少这些事实时，可用 [Kconfig 中的兼容选项](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/Kconfig) 确认**确实启用 bounce buffer** 的配置，它不能让不支持的芯片获得无 bounce 交接能力。

当前源码声明 Present 2.0.0、LVGL binding 0.2.0。使用本地源码时，在应用的 `main/idf_component.yml` 添加以下依赖并替换路径。版本声明不代表 Registry 中已经发布；LVGL 用户应按 [绑定说明](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md) 同时指定两个本地组件。

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

## 创建 Presenter

先通过 BSP 或 `esp_lcd` 初始化面板。下面借用已有 RGB 面板，由 AUTO 按实际驱动 framebuffer 池选择模式：

```c
#include "esp_display_present.h"

// BSP 已初始化受支持的 480x480 RGB 面板，驱动至少提供两张 framebuffer。
esp_err_t create_presenter(esp_lcd_panel_handle_t panel,
                           esp_display_presenter_t **out_presenter)
{
    const esp_display_presenter_panel_config_t lcd = {
        .width = 480,
        .height = 480,
        .hw = {
            .panel = panel,
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        },
    };
    return esp_display_presenter_open(&lcd, ESP_DISPLAY_PRESENT_MODE_AUTO,
                                      out_presenter);
}
```

MIPI 使用 `ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI`；Panel IO 使用 `ESP_DISPLAY_PRESENT_PANEL_IO` 并填写 `hw.io`。启用 TE 时再填写 `hw.te_enabled` 和与接线、总线一致的 `hw.te_sync`。在 Presenter 删除成功前，保持 panel、IO 及其存储有效，由 Present 管理它注册的完成回调。

`open()` 接收面板的**物理宽高**，通过 `hw.rotation` 指定逻辑方向。需要设置 tile 行数、T1/T2、PSRAM 偏好或 TE 的 C1/C2 时用 `open_ex()`；需要显式 framebuffer、脏区上限或超时时间时用 `create()`，其中 `width`、`height` 是**逻辑宽高**。创建后用 `get_caps()`、`get_effective_config()` 查询实际布局、模式和缓冲池。

初始化 RGB/MIPI 面板前，可用 `esp_display_present_get_required_frame_buffer_count()` 查询建议 framebuffer 数量；该查询不分配缓冲。

## 选择模式与流水线组合

下表列出 `open()` / `open_ex()` / `create()` 创建的**单个 Presenter 实例**。输入 tile 是分块绘制缓冲；整屏绘制缓冲供渲染器绘制，整帧合成缓冲供 TE 输出，两者职责不同。

**驱动帧缓冲由驱动持有，表中的 Present 像素缓冲在它之外另行分配，两列需要分别计算。** 后台任务数量不包含应用／LVGL、驱动任务、ISR 和定时器回调。代码中的 `backend` 任务负责显示推进，`pixel` 任务负责像素处理；表中直接写明由几个任务承担。

RGB/MIPI 按原生 framebuffer 像素布局使用，不需要软件交换 RGB565 高低字节；下表按旋转区分其路径。Panel IO 的线上字节顺序按面板要求配置，字节交换与 RGB/BGR 分量顺序是不同设置。

| 面板／模式 | 旋转 | 驱动帧缓冲 | Present 分配的像素缓冲 | Present 后台任务 |
|---|---|---|---|---|
| RGB/MIPI `DOUBLE_FULL` / `TRIPLE_FULL` | 0° | 2 或 3 张 | 不另分配像素缓冲，直接绘制驱动帧缓冲 | 1 个显示推进任务 |
| RGB/MIPI `DOUBLE_FULL` / `TRIPLE_FULL` | 90° / 180° / 270° | 2 或 3 张，保留 | 另加 1 张整屏绘制缓冲，由这些驱动帧缓冲共用 | 1 个任务：推进显示并处理像素 |
| RGB/MIPI `DOUBLE_DIRECT` | 0° | 2 张 | 不另分配像素缓冲，直接在驱动帧缓冲绘制脏区 | 1 个任务：推进显示并补全内容 |
| RGB/MIPI `DOUBLE_DIRECT` | 90° / 180° / 270° | 2 张，保留 | 另加 1 张整屏绘制缓冲，由两张驱动帧缓冲共用 | 1 个任务：推进显示并处理像素 |
| RGB/MIPI `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | 0° / 90° / 180° / 270° | 2 或 3 张 | 1 块输入 tile；直接复制／旋转到驱动帧缓冲，不另加整帧 | 1 个任务：推进显示并处理像素 |
| RGB/MIPI `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | 0° / 90° / 180° / 270° | 2 或 3 张 | 2 块输入 tile；直接复制／旋转到驱动帧缓冲，不另加整帧 | 2 个任务：显示推进、像素处理各 1 个 |
| IO `NONE`，`CALLER` / `DEFAULT` | 0° / 90° / 180° / 270° | 无 | T=1/2 块内部或 PSRAM D；需要转换或无法直 DMA 时，懒分配 2 块有界内部 S¹ | 0；调用任务处理像素和提交 |
| IO `NONE`，`BACKGROUND`（WORKER） | 0° / 90° / 180° / 270° | 无 | T=1/2 块内部或 PSRAM D；同上，不分配等容量 X | 1 个 backend；负责转换、分片和提交 |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | 无 | 1 块输入 tile，1 张整帧合成缓冲；发送中转缓冲见注¹ | 1 个任务：推进显示并处理像素 |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | 无 | 2 块输入 tile，1 张整帧合成缓冲；发送中转缓冲见注¹ | 1 个任务：推进显示并处理像素 |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | 无 | 1 块输入 tile，2 张整帧合成缓冲；发送中转缓冲见注¹ | 2 个任务：显示推进、像素处理各 1 个 |
| IO `TE_SYNC` | 0° / 90° / 180° / 270° | 无 | 2 块输入 tile，2 张整帧合成缓冲；发送中转缓冲见注¹ | 2 个任务：显示推进、像素处理各 1 个 |

¹ NONE 与 TE 共用 `CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES`，每个实例各自持有 2 块 S，每块预算默认 16 KiB，按物理整行取整；原 TE 配置名通过 `sdkconfig.rename` 迁移。NONE 在首次需要时分配；TE 的非直传 PSRAM 合成帧在创建时分配。两块交替预填／发送，最多一片 DMA 在途，尾片只发送实际像素，不填充线上长度；物理单行超过预算时拒绝该路径。任务栈、元数据及驱动内部缓冲另计。

### 自动选择

`AUTO` 先解析为具体模式，再按上表执行：RGB/MIPI 有三张驱动 FB 时选 `TRIPLE_PARTIAL`，两张时选 `DOUBLE_PARTIAL`；Panel IO 的 TE 配置有效时选 `TE_SYNC`，否则选 `NONE`。

RGB/MIPI 不支持 `NONE` 和 `TE_SYNC`。`mode` 清零得到 `NONE`，需要自动选择时须显式填写 `AUTO`。TE 效果取决于面板时序和传输预算，并非所有配置都能消除撕裂。

### 配置缓冲

通过 `open_ex()` 的 `drawbuf` 参数配置；使用 `create()` 时填写 `target.drawbuf`：

- `lines`：每块输入 tile 的行数。0 使用 `CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES` 的预算（默认 32 KiB），按逻辑整行取整，至少一行、不超过逻辑高度。显式行数覆盖该默认值；此预算不是单行大小的硬上限。
- `buffers`：输入 tile 数 T，取 1 或 2。0 自动选择；仅自动选择的双 tile 分配失败时可回退单 tile。FULL/DIRECT 不使用此池。
- `in_psram`：优先把输入 tile 放到 PSRAM；实际可能回退到内部 DMA 内存。
- `te_compose_buffers`：TE 合成帧数 C，0/1 为单帧，2 为双帧；仅 `TE_SYNC` 生效，与 tile 数独立。

### 异步处理与旋转

IO `NONE` 只选择执行任务：`target.io_submit = CALLER` 或 `DEFAULT`（零值）在调用任务执行转换、分片与发送；`BACKGROUND` 使用一个 backend 输出任务，默认优先级 7。内部／PSRAM、swap、旋转都不再改变任务数。该字段通过 `create()` 配置；`open()` / `open_ex()` 使用 `DEFAULT`。其他模式仍须保持 `DEFAULT`。

推荐三个组合：CALLER＋单内部 D；BACKGROUND＋双内部 D；BACKGROUND＋双 PSRAM D。1/2 个 D 和两种内存位置均可使用相同实现。NONE 仍使用 PARTITION 合同：D 可覆盖整屏，但不提供 FB 的 FULL 持久画布语义。

PSRAM 直 DMA 仅在芯片、IO 配置、实际地址和长度都满足条件时使用；需要 swap 且可直传时原地交换。旋转后按物理行填 S；对齐不满足时回退 S，不为了 64 字节 DMA 长度扩大脏区或填充线上像素。非连续源行只是读取方式，不单列一种工作模式。

运行时 `set_rotation()` 不增加任务、不分配等容量 X，原地 swap 的实例也可切换到旋转分片输出。90°／270° 会交换逻辑宽高；切换前须排空，切换后重新查询布局，并处理 tile 容量无法容纳一行等尺寸错误。

TE 单合成帧共用显示推进任务执行异步像素队列，不另建像素任务；两张合成帧保留独立像素任务，以允许合成与传输重叠。任务合并不改变异步请求、完成回调和缓冲所有权。

两个输入 tile 可以交替使用，让应用绘制与后台像素处理重叠。TE 有两张合成帧时，可在上一帧传输期间合成下一帧；只有一张时，要等传输释放该帧后才能重写。增加缓冲不会自动增加后台任务，也不保证帧率提高。

异步表示像素工作可交给后台推进，不表示一定使用 PPA／DMA2D；CPU 也可执行。像素请求仍逐笔处理，总线传输是否异步由输出路径决定。

RGB/MIPI 扫描输出模式和 TE 双合成帧的显示推进任务默认优先级均为 7；独立像素任务会继承首个提交者的优先级。提交者优先级低于 7 时，显示输出可优先推进，减少错失切帧窗口和并行合成下的续传间隙；TE 单合成帧保留默认优先级 5。仍须结合应用优先级、XIP、总线带宽和实测传输时长核对 TE 窗口。

### TE 接入条件与限制

先在面板驱动中启用 TE 输出并连接信号，再按实际硬件填写 `te_enabled`、`te_sync.gpio_num`、`bus_freq_hz` 和 `data_lines`。总线参数用于首次传输估算；后续准入使用实测时序，包含分片之间的间隙。

启动时会采样 GPIO 约 9 ms 判断极性，前提是消隐脉冲较短、空闲电平持续较长；不增加后台任务或周期采样。其他 TE 波形需要单独验证。缓冲数量不能保证传输一定落在 TE 窗口内，详见 [TE 时序与准入](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md)。

### 内存与实际配置

输入 tile、TE 合成帧、驱动帧缓冲和转换／中转缓冲是**分别占用的存储**。驱动帧缓冲和 Present 像素缓冲已在表中分列；各路径的分配与回收规则如下：

- RGB/MIPI FULL/DIRECT：0° 时直接借用驱动 FB；90°／180°／270° 时增加 **1 张共享逻辑整帧**，不是每张驱动 FB 各加一张。该逻辑帧优先放 PSRAM，可能回退内部 DMA 内存；旋转回 0° 时释放。
- PARTIAL：分配 T 块输入 tile，直接复制／旋转到候选驱动 FB；旋转不另加整帧画布，也不另建转换 tile 池。
- IO NONE：只分配 T 块 D；0°、行连续且当前源满足 DMA 约束时直接发送 D，RGB565 需要时原地 swap16 并执行 cache writeback；拒绝提交会还原像素。旋转、非连续源行或无法直 DMA 时，使用一对有界 S，边读取 D 边生成物理行，融合旋转与 swap。D 保留到整个逻辑区域传输完成；S 在各自 DMA 完成后复用。不会分配与 D 等容量的 X，也不创建 pixel 任务。
- TE_SYNC：分配 T 块输入 tile 和 C 张物理整帧合成图像，旋转直接合成到图像，不再加旋转整帧。合成图像优先放 PSRAM，可能回退内部 DMA 内存。不能直 DMA 的 PSRAM 图像另用上述两块中转缓冲，大小独立于 T 和 C。整帧保留到逻辑传输完成；停止或超时会禁止发送后续片，但已进入驱动的片仍须等真实完成回调后回收。停止超时需保留实例重试；已经写入面板的部分不能撤销。

这些数量仍不是应用的全部内存：驱动内部的 RGB bounce buffer、DMA 描述符、渲染前端（例如 LVGL）另行分配的绘制缓冲，以及任务栈、元数据和对齐开销均需另计。一张 480×480 RGB565 图像为 450 KiB，一块 480×32 RGB565 tile 为 30 KiB。

创建后用 `get_effective_config()` 确认实际模式、`tile_count`、`image_count`、内存域和降级标志，用 `get_caps()` 查询绘制合同；这些接口不枚举所有懒分配缓冲，实际内存还需按运行路径测量。详见 [模式与缓冲合同](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md)。

## 渲染与释放

自定义渲染器通常按 `begin_next_frame()` → `acquire_buffer()` → 绘制并 `submit_buffer()` → `commit_frame()` 推进。PARTITION 可在一帧内多次获取和提交区域。遵守返回的绘制合同与租约容量，提交后的缓冲不能继续写入。LVGL 用户由绑定完成这些操作。

渲染调用由拥有 Presenter 的生产者任务执行，`open()` 首先绑定调用任务。自定义渲染器换任务时，在帧间调用 `rebind_producer()`；若下一 adapter 无法自行重绑，由当前所有者排空并 `release_producer()`，再启动下一渲染器，其首个获准帧会认领所有权。LVGL `start()` 会自动绑定。

旋转或切换渲染器前，先暂停生产并 `quiesce()`；LVGL 须先按 [绑定说明](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md) 停止绑定。旋转后重新查询布局。销毁 Presenter 前，先停止并销毁渲染绑定；Presenter 删除成功后，再释放应用持有的面板和 IO。

超时或取消不代表硬件已停止读取。stop/delete 无法完成时，保留对象和借用存储供后续重试，不在错误分支强行释放。故障锁存后须安全销毁并重建，原始原因用 `get_fault_error()` 查询。硬件加速是可选能力，取决于 SDK、SoC 和像素布局。详见 [所有权与恢复规则](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/STATE_MACHINE.md)。

## 示例与参考

- [LVGL benchmark](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark)：面板初始化与 LVGL 接入。
- [GSP/LVGL 交接](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff)：生产者所有权和可重试恢复。
- [兼容与迁移](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md)、[TE 时序](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md)、[公开 API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h)。
- [Host 回归与目标 smoke](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md)：正式测试入口。
