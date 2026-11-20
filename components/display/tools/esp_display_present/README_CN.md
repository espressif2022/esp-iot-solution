# ESP Display Present

[English](README.md) | 中文

`esp_display_present` 为 LVGL、GSP 和自绘渲染器提供统一的显示输出接口。
它基于 `esp_lcd` 管理显示缓冲、像素变换和送屏同步，让应用按屏幕与刷新需求选择呈现方式。

```text
LVGL 9 → esp_lv_present ──┐
GSP / 自绘渲染器 ──────────┤
                         ▼
                esp_display_present
             缓冲管理 · 旋转 · 同步送屏
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
        SPI / QSPI              RGB / MIPI DSI
       发送像素到屏幕             切换显示帧缓冲
```

应用负责初始化总线和面板，渲染器负责生成像素，Present 负责把画面送到屏幕。

## 提供哪些能力

- **多种刷新方式**：分块绘制、整帧重绘、完整画布上的脏区更新。
- **显示同步**：Panel IO 的 TE 同步，以及 RGB／MIPI 的双／三帧缓冲切换。
- **像素变换**：RGB565、RGB888，0°／90°／180°／270° 旋转，以及 Panel IO 的 RGB565 字节交换。
- **内存与流水线配置**：绘制缓冲可优先使用内部内存或 PSRAM，通过缓冲数量安排绘制与输出重叠；Panel IO 需要中转时使用有界内存，适合大块或整屏输入。

## 怎么选择模式

先按屏幕接口选择，再看应用如何绘制。模式名省略 `ESP_DISPLAY_PRESENT_MODE_` 前缀。

| 屏幕接口 | 使用场景 | 模式 | 主要内存需求 |
|---|---|---|---|
| SPI／QSPI | 分块绘制，直接发送 | `NONE` | 1／2 块绘制缓冲，按需中转 |
| SPI／QSPI + TE | 合成完整画面，按 TE 窗口发送 | `TE_SYNC` | 绘制缓冲 + 1／2 张合成帧，按需中转 |
| RGB／MIPI DSI | 用小块绘制缓冲完成局部刷新 | `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL` | 绘制缓冲 + 2／3 张驱动帧缓冲 |
| RGB／MIPI DSI | 每帧完整重绘 | `DOUBLE_FULL` / `TRIPLE_FULL` | 2／3 张驱动帧缓冲 |
| RGB／MIPI DSI | 在完整画布上只更新脏区 | `DOUBLE_DIRECT` | 2 张驱动帧缓冲 |

FULL／DIRECT 旋转时另需一张共享逻辑画布。Panel IO 即使使用整屏大小的绘制缓冲，仍按分块提交接口使用。

`NONE` 不提供防撕裂保证。TE 需要面板输出 TE 信号并匹配实际传输时序；RGB／MIPI 通过帧缓冲交接分开绘制与扫描。

显式设置 `AUTO` 时，Panel IO 根据 TE 配置选择 `TE_SYNC` 或 `NONE`；RGB／MIPI 根据实际驱动帧缓冲数量选择 `DOUBLE_PARTIAL` 或 `TRIPLE_PARTIAL`。
配置全零时是 `NONE`，而 RGB／MIPI 不支持 `NONE`。

### 缓冲与后台任务

- **D（Draw buffer）**：渲染器绘制 tile／条带的缓冲，配置 1 或 2 块，可优先放内部内存或 PSRAM。
- **C（Compose buffer）**：TE 模式的整帧合成缓冲，配置 1 或 2 张。
- **F（Framebuffer）**：RGB／MIPI 驱动的整帧扫描缓冲，由模式选择使用 2 或 3 张。
- **S（Staging buffer）**：Panel IO 需要转换或无法直传时使用的内部 DMA 中转缓冲，两块，默认每块不超过 16 KiB。

D 数量与 C／F 数量独立。例如 `2 D + 3 F` 表示两块绘制缓冲配合三张驱动帧缓冲，
并不是五张整屏画布。FULL／DIRECT 直接绘制完整表面，不使用 D 池。

后台任务由模式和实际缓冲数量自动决定，无需单独开启。
`backend` 负责推进提交和完成处理；独立的 `pixel` 任务负责拷贝／旋转／合成。
下表只统计 Present 创建的任务，不包含 GUI、驱动和系统任务。

| 模式与缓冲组合 | 后台任务 | 工作分配与并行关系 |
|---|---|---|
| `NONE`：1 D | 0 | 调用任务完成像素处理和提交；这块 D 传输完成后才能再次绘制 |
| `NONE`：2 D | 1 个 backend | backend 处理并发送 D0，应用可同时绘制 D1 |
| `TE_SYNC`：1／2 D + 1 C | 1 个 backend | backend 兼做合成与发送；同一张 C 的合成和传输交替进行 |
| `TE_SYNC`：1／2 D + 2 C | backend + pixel，共 2 个 | pixel 合成下一张 C，backend 发送上一张 C |
| `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`：1 D + 2／3 F | 1 个 backend | 拷贝／旋转写入 F，backend 推进切屏；单 D 的同步拷贝优化见下文 |
| `DOUBLE_PARTIAL` / `TRIPLE_PARTIAL`：2 D + 2／3 F | backend + pixel，共 2 个 | 应用绘制 D1，pixel 处理 D0，显示外设扫描已提交的 F |
| FULL／DIRECT：2／3 F，旋转时另加逻辑画布 | 1 个 backend | 应用绘制完整表面；转换按路径执行，backend 推进切屏；DIRECT 只使用 2 F |

**同步与异步**：单 D 的 FB PARTIAL 在选用 CPU 且旋转为 0°／180°时，直接在调用任务拷贝；
其他像素请求交由后台处理。FULL 的 CPU 180°整帧转换也有同步路径。
因此，存在 backend 任务不代表每次像素处理都异步；没有后台任务也不代表 DMA 同步完成。

**配置入口**：`drawbuf.buffers` 决定 D 数量，`drawbuf.te_compose_buffers` 决定 C 数量，
`DOUBLE_*`／`TRIPLE_*` 决定 F 数量。自动配置可能因内存不足减少 D，实际结果可用
`get_effective_config()` 查询。`NONE` 中旋转、swap 或 PSRAM 中转不会再增加 pixel 任务。

## 接入要求

- ESP-IDF 6.0 及以上，像素格式与已初始化的面板匹配。
- 当前 Panel IO 发送适配器支持 SPI／QSPI 的完成回调协议，其他接口需单独适配。
- RGB／MIPI 至少需要两张驱动帧缓冲。ESP32-S3 RGB 需要启用驱动 bounce buffer；ESP-IDF 6.0 下 MIPI-DSI 仅提供 `on_refresh_done` 时，`CONFIG_ESP_REV_MIN_FULL >= 300` 的多缓冲切换会多等一个刷新周期以保证 DMA 安全退休，ESP-IDF 6.1+ 直接使用 `on_frame_buf_complete`。

这份源码使用本地组件接入，在应用的 `main/idf_component.yml` 中指定路径：

```yaml
dependencies:
  espressif/esp_display_present:
    version: "^2.0.0"
    override_path: /path/to/esp-iot-solution/components/display/tools/esp_display_present
```

源码版本号不代表 Registry 已发布。LVGL 用户还需按[绑定说明](../esp_lv_present/README_CN.md)指定配套的本地 `esp_lv_present`。

## 创建 Presenter

应用先初始化面板，再创建 Presenter。下面以 RGB 面板和自动模式为例：

```c
#include "esp_display_present.h"

const esp_display_presenter_panel_config_t lcd = {
    .width = 480,
    .height = 480,
    .hw = {
        .panel = panel,  // 已初始化的面板句柄
        .panel_type = ESP_DISPLAY_PRESENT_PANEL_RGB,
        .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
    },
};
esp_display_presenter_t *presenter = NULL;
ESP_ERROR_CHECK(esp_display_presenter_open(
    &lcd, ESP_DISPLAY_PRESENT_MODE_AUTO, &presenter));
```

Panel IO 还需提供 `.hw.io`，设置 `PANEL_IO` 类型及所需的字节序和 TE 配置。
`open_ex()` 可配置绘制缓冲大小、数量和内存偏好；完整字段见[公共 API](include/esp_display_present.h)与[配置说明](include/esp_display_present_config.h)。

**LVGL 9**：使用 [esp_lv_present](../esp_lv_present/README_CN.md) 绑定，应用运行 UI 任务。
**GSP／自绘渲染器**：通过公共 API 获取缓冲、绘制并提交画面。

```text
获取绘制缓冲 → 绘制 → 提交画面
      ↑                 │
      └──── 下一帧 ──────┘
```

## 示例与进一步阅读

- [LVGL benchmark](../../../../examples/display/gui/lvgl_present_benchmark/README.md)：面板初始化、LVGL 接入和性能测试。
- [GSP／LVGL 交接](../../../../examples/display/gui/gsp_lvgl_present_handoff/README.md)：多个渲染器共用显示输出。
- [模式与缓冲](docs/MODE_ARCHITECTURE.md)、[TE 配置与时序](docs/TE_TIMING.md)、[兼容与迁移](COMPATIBILITY.md)。
- [Host 测试](tests/host/README.md)、[内部状态机](STATE_MACHINE.md)：验证与维护参考。
