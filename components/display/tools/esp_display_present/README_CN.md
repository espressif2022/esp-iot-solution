# ESP Display Present

[English](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/README.md)

`esp_display_present` 管理 ESP LCD 面板的帧提交、缓冲所有权和显示同步。应用初始化面板和总线，Present 接收渲染器提交的像素并管理其使用周期。

LVGL 9 使用 [esp_lv_present](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md) 绑定；自定义渲染器使用 [Presenter API](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/include/esp_display_present.h)。

## 选择模式与流水线组合

先按面板和绘制方式选模式，再配置缓冲数量。表内模式名均省略 `ESP_DISPLAY_PRESENT_MODE_` 前缀。

| 面板与需求 | 模式 | 渲染器交付的内容 |
|---|---|---|
| Panel IO，普通区域刷新 | `NONE` | PARTITION：逐块提交区域 |
| Panel IO，按 TE 窗口发送 | `TE_SYNC` | PARTITION：逐块合成，再发送整帧 |
| RGB/MIPI，分块绘制 | `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | PARTITION：逐块写入候选 framebuffer |
| RGB/MIPI，每帧完整重绘 | `DOUBLE_FULL` / `TRIPLE_FULL` | FULL：提交完整帧 |
| RGB/MIPI，在完整画面中更新脏区 | `DOUBLE_DIRECT` | DIRECT：提交整帧缓冲内的脏区 |

`AUTO` 的选择规则：

- RGB/MIPI：三张驱动 framebuffer 选 `TRIPLE_PARTIAL`，两张选 `DOUBLE_PARTIAL`。
- Panel IO：TE 配置有效时选 `TE_SYNC`，否则选 `NONE`。

**清零的 mode 是 `NONE`，不是 `AUTO`。** RGB/MIPI 不支持 `NONE`、`TE_SYNC`；Panel IO 不支持 FB 的 FULL、DIRECT、PARTIAL 模式。

## 接入要求与依赖

- ESP-IDF 6.0 及以上；输入为 RGB565 或 RGB888，须与已初始化的面板配置匹配。
- RGB/MIPI 至少需要两张驱动 framebuffer，并支持可确认的扫描交接。DSI 还需要 DPI 帧完成和 VSYNC 回调，ESP-IDF 6.0 不提供这些回调。
- Panel IO 需要 panel、IO 两个句柄；当前发送适配器按 SPI/QSPI 的完成回调协议实现。

ESP32-S3 RGB 须在驱动中启用 bounce buffer。RGB 使用连续刷新和未经驱动变换的坐标，逻辑旋转交给 Present。旧驱动无法查询实际配置时，只能对确实启用 bounce buffer 的面板使用 [Kconfig 兼容选项](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/Kconfig)。其他扫描协议的支持范围见 [模式与输出适配](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md)。

使用本地源码时，在应用的 `main/idf_component.yml` 添加依赖并替换路径：

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

源码版本声明不代表 Registry 已发布。LVGL 用户须按 [绑定说明](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_lv_present/README_CN.md) 同时指定两个本地组件。

## 创建 Presenter

### RGB/MIPI：借用驱动 framebuffer

先由 BSP 或 `esp_lcd` 初始化面板，再创建 Presenter：

```c
#include "esp_display_present.h"

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

MIPI 使用 `ESP_DISPLAY_PRESENT_PANEL_MIPI_DSI`。初始化驱动前，可用 `esp_display_present_get_required_frame_buffer_count()` 查询建议的 framebuffer 数量；查询本身不分配缓冲。

### Panel IO：双内部绘制缓冲，后台输出

下面是普通 SPI/QSPI RGB565 刷新的配置示例。`swap_bytes` 须与面板驱动需要的线上字节序一致。

```c
#include "esp_display_present.h"

esp_err_t create_io_presenter(esp_lcd_panel_handle_t panel,
                              esp_lcd_panel_io_handle_t io,
                              esp_display_presenter_t **out_presenter)
{
    const esp_display_presenter_config_t config = {
        .width = 480,
        .height = 480,
        .pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
        .target = {
            .hw = {
                .panel = panel,
                .io = io,
                .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
                .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
                .swap_bytes = true,
            },
            .fb.mode = ESP_DISPLAY_PRESENT_MODE_NONE,
            .drawbuf = {.lines = 32, .buffers = 2, .in_psram = false},
            .io_submit = ESP_DISPLAY_PRESENT_IO_SUBMIT_BACKGROUND,
        },
    };
    return esp_display_presenter_create(&config, out_presenter);
}
```

| 创建入口 | 宽高含义 | 用途 |
|---|---|---|
| `open()` | 物理宽高 | 默认缓冲策略 |
| `open_ex()` | 物理宽高 | 自定义 tile 行数、数量、PSRAM 偏好及 TE 合成帧数 |
| `create()` | 逻辑宽高 | 还可设置 `io_submit`、超时、脏区上限及显式 framebuffer |

旋转由 `hw.rotation` 指定。创建后用 `get_caps()` 查询绘制合同，用 `get_effective_config()` 核对实际模式、缓冲数量、内存位置和降级标志。panel、IO 及驱动存储须保持有效，直到 Presenter 删除成功；其间由 Present 管理注册的完成回调。

## Panel IO NONE：绘制与发送

**D 是渲染器绘制的缓冲，S 是内部 DMA 分片中转缓冲。** 推荐按下面三个组合选择；1/2 块 D 与两种内存位置也可独立搭配。

| 组合 | `io_submit` | `drawbuf.buffers` | `drawbuf.in_psram` | Present 后台任务 |
|---|---|---|---|---|
| 简单小 tile，最低任务开销 | `CALLER` | 1 | `false` | 0 |
| 常规 tile，绘制与输出重叠 | `BACKGROUND` | 2 | `false` | 1 个 backend |
| 大 tile／整屏 D，节省内部绘制内存 | `BACKGROUND` | 2 | `true` | 1 个 backend |

表中 `CALLER` / `BACKGROUND` 均省略 `ESP_DISPLAY_PRESENT_IO_SUBMIT_` 前缀。`DEFAULT` 等同 `CALLER`；`open()`、`open_ex()` 使用 `DEFAULT`。只有 `NONE` 允许显式设置此策略。

CALLER 在调用任务中处理像素并提交驱动，可能阻塞。BACKGROUND 把这些工作交给一个 backend 任务。swap、旋转、D 的内存位置均不再增加 pixel 任务。

### 每次提交如何选择输出路径

| 输入条件 | 像素处理 | DMA 源 |
|---|---|---|
| 0°、行连续、实际地址与长度满足 DMA 约束 | 无需复制；RGB565 需要时原地 swap16，随后 cache writeback | D |
| 旋转、源行不连续，或不能直接 DMA | 读取 D，融合复制／旋转／swap，按物理行填入 S | S |

S 在第一次需要时分配，共两块，默认每块不超过 16 KiB；当前片发送时可填下一块，最多一片 DMA 在途。尾片只发送实际像素，不为 DMA 对齐扩大脏区或补线上数据。**内部中转内存不随 D 的容量增长，不分配与 D 等大的转换池。**

PSRAM 直 DMA 同时取决于芯片、IO 配置及本次请求的地址和长度；不满足时回退 S。驱动不能查询该能力时，`hw.spi_psram_dma_direct` 须与实际 IO 创建标志一致。RGB565 字节交换与 RGB/BGR 分量顺序是两个不同设置。

### D 何时可以再绘制

```text
绘制 D → 提交 → 直传 D，或通过 S 分片发送
                         ↓
                 整个区域传输完成 → D 可再次获取
```

函数返回不代表 DMA 已完成；提交后不能继续写 D。S 也要等自己的 DMA 完成后才能重写。双 D 允许绘制另一块缓冲，但输出跟不上时仍会产生背压。

D 可以设置为整屏大小，例如 `drawbuf.lines = height`；它仍是 PARTITION 的租用缓冲，不能依赖跨帧内容保留。两张 480×480 RGB565 D 共占 900 KiB，需另计 S、元数据和任务栈。

## Panel IO TE_SYNC：先合成，再按窗口发送

TE 模式把 D 合成到物理整帧 C，再按 TE 时序准入输出。D 数与 C 数独立配置：

| D 数 | `te_compose_buffers` | 合成帧 | Present 后台任务 |
|---|---|---|---|
| 1 或 2 | 0 / 1 | 1 张 C | 1 个 backend，兼做像素合成 |
| 1 或 2 | 2 | 2 张 C | backend＋pixel，共 2 个 |

一张 C 要等其传输结束才能重写；两张 C 允许上一帧发送时合成下一帧。旋转直接写入 C，不另加旋转整帧。

C 优先放 PSRAM，可回退内部 DMA 内存。内部 DMA C 或已确认可直 DMA 的 PSRAM C 直接提交；不能直 DMA 的 PSRAM C 在创建时另分配两块 S，预算与 NONE 共用，但缓冲属于各自实例。

使用前先在面板驱动启用 TE 输出并接线，再设置 `hw.te_enabled`、`hw.te_sync.gpio_num`、`bus_freq_hz` 和 `data_lines`。总线参数用于首次发送估算，后续使用实测时序，包含分片间隙。启动极性检测假定短消隐脉冲、长空闲电平；其他波形需单独验证。**TE 配置和缓冲数量本身不能保证消除撕裂。** 详见 [TE 时序与准入](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/TE_TIMING.md)。

## RGB/MIPI：构帧与扫描交接

驱动持有 2 或 3 张 framebuffer（FB）。下表的 Present 缓冲是在驱动 FB 之外另行分配的；RGB/MIPI 使用原生像素字节序，不配置软件 RGB565 swap。

| 模式与旋转 | Present 另分配的像素缓冲 | Present 后台任务 |
|---|---|---|
| FULL / DIRECT，0° | 无，直接绘制驱动 FB | 1 个 backend |
| FULL / DIRECT，90° / 180° / 270° | 1 张共享逻辑整帧 | 1 个 backend，兼做像素处理 |
| PARTIAL，任意旋转，单 D | 1 块 D，复制／旋转到候选 FB | 1 个 backend，兼做像素处理 |
| PARTIAL，任意旋转，双 D | 2 块 D，复制／旋转到候选 FB | backend＋pixel，共 2 个 |

FULL 对应 `DOUBLE_FULL` / `TRIPLE_FULL`；DIRECT 对应 `DOUBLE_DIRECT`；PARTIAL 对应 `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`。旋转的 FULL/DIRECT 共用一张逻辑画布，优先放 PSRAM，旋转回 0° 时释放。PARTIAL 不增加逻辑整帧。

**复制完成与扫描退休是两个时间点。** D 在复制完成后可归还，驱动 FB 仍须按扫描交接协议退休，才能再次绘制。后台任务数不包含应用／LVGL、驱动任务、ISR 或定时器回调；硬件加速取决于 SDK、SoC 和布局，后台处理也可能使用 CPU。

## 公共缓冲与任务配置

`create()` 使用 `target.drawbuf`；`open_ex()` 使用其 `drawbuf` 参数：

| 字段 | 含义 |
|---|---|
| `lines` | D 的逻辑行数；0 按默认 tile 字节目标取整，至少一行、不超过逻辑高度 |
| `buffers` | D 数，1 或 2；0 自动选择，只有自动选择的双 D 分配失败时允许回退单 D |
| `in_psram` | D 优先放 PSRAM，实际可能回退内部 DMA 内存 |
| `te_compose_buffers` | 仅 TE_SYNC 生效；0/1 为单 C，2 为双 C |

FULL/DIRECT 不使用输入 tile 池。一块 480×32 RGB565 D 占 30 KiB；渲染前端另行分配的缓冲、驱动 bounce buffer、DMA 描述符、对齐开销、任务栈和元数据均需另计。`get_effective_config()` 不枚举所有懒分配缓冲。

menuconfig 的 `Display presenter` 菜单提供以下设置：

| Kconfig | 默认值 | 作用 |
|---|---|---|
| `CONFIG_ESP_DISPLAY_PRESENT_DEFAULT_TILE_BYTES` | 32 KiB | `lines = 0` 时的 D 字节目标；单行可超过此目标 |
| `CONFIG_ESP_DISPLAY_PRESENT_IO_STAGING_SLOT_BYTES` | 16 KiB | 每块 S 的字节上限；按物理整行取整，物理单行超过预算时拒绝该路径 |
| `CONFIG_ESP_DISPLAY_PRESENT_BACKEND_TASK_PRIORITY` | 0 | backend 优先级；0 在创建／成功绑定生产者时取其当前优先级，非零为应用指定的固定优先级 |

NONE 与 TE 共享 S 的预算配置，各实例拥有独立缓冲；旧 TE 配置名由 `sdkconfig.rename` 迁移。backend 优先级不随逐帧提交、完成回调或场景变化而调整；独立 pixel 任务沿用首个提交者优先级。优先级和缓冲数量都不能替代实际调度与带宽验证。

## 渲染、换绑与释放

自定义渲染器的基本顺序：

```text
begin_next_frame()
    → acquire_buffer() → 绘制 → submit_buffer()   // PARTITION 可重复
    → commit_frame()
```

使用返回的合同、stride 和租约容量。租约的 `resolve_rows` 回调可按选定 stride 计算容得下的行数；不能越过 `capacity_bytes`，也不能在提交后继续写。LVGL 由绑定完成这些操作。

渲染调用由拥有 Presenter 的生产者任务执行。创建时先绑定调用任务；自定义渲染器换任务时，在帧间调用 `rebind_producer()`。若下一 adapter 不能自行换绑，由当前所有者排空并 `release_producer()`，再启动下一渲染器。LVGL `start()` 自动绑定。

切换旋转或渲染器时，先暂停生产并 `quiesce()`；LVGL 先按绑定说明停止绑定。90°／270° 会交换逻辑宽高，切换后重新查询布局。NONE 旋转不增加任务或等容量转换池；其他模式的画布变化见对应表格。

错误与关闭须遵守：

- NONE CALLER 提交被拒绝时保留原租约和像素，可修正原因后显式重试。
- NONE 后台已接收的请求遇到 S 分配失败会以 `NO_MEM` 失败并安全归还 D；通道忙则等待真实完成，不空转重试。
- 超时或取消不代表硬件停止读取。stop/delete 未完成时保留对象及借用存储，供后续重试；故障锁存后安全销毁并重建，原始错误用 `get_fault_error()` 查询。
- 先停止并销毁渲染绑定，再删除 Presenter；删除成功后才释放应用持有的 panel 和 IO。

## 示例与参考

- [LVGL benchmark](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/lvgl_present_benchmark)：面板初始化与 LVGL 接入。
- [GSP/LVGL 交接](https://github.com/espressif/esp-iot-solution/tree/master/examples/display/gui/gsp_lvgl_present_handoff)：生产者换绑与可重试恢复。
- [模式与缓冲合同](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/docs/MODE_ARCHITECTURE.md)：模式映射、输出协议和内部职责。
- [所有权与恢复](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/STATE_MACHINE.md)、[兼容与迁移](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/COMPATIBILITY.md)。
- [Host 回归与目标 smoke](https://github.com/espressif/esp-iot-solution/blob/master/components/display/tools/esp_display_present/tests/host/README.md)：正式测试入口；性能数据须绑定实际提交与板级配置。
