# 私有源码目录

本说明对应2026-11-06目录整理。目录表达职责归属；现有跨层类型依赖尚未全部解除，不能把本图当作已经严格隔离的依赖层级。

```text
src/
  esp_display_presenter.c      公共API、会话和兼容语义
  sources.cmake               IDF与Host共用的显式源码清单
  core/                       Engine、完成跟踪、传输期限跟踪
  config/                     模式策略、布局/内存规划、几何校验
  resources/                  硬件/内存资源组装、预算汇总与生命周期
  backend/                    FB/GRAM/TE协议、帧状态、覆盖与历史修复
  pixel/                      像素任务与转换接口
    executor/                 CPU/PPA/DMA2D执行插件
  platform/                   OS、LCD驱动适配、TE定时与同步
```

## 放置规则

- 新增文件按职责归入模块，src根目录保留公共入口与源码清单。暂不继续细分backend子目录；共享帧状态和历史修复仍在同一模块。
- 私有头文件与所属模块一起放置。引用使用相对src根目录的模块路径，例如`core/present_presenter.h`、`platform/present_os.h`。构建只添加src根目录，不把所有模块目录加入搜索路径。
- include目录继续只承载公共API。esp_lv_present及维护中的测试依赖私有接口时，显式引用模块路径；这些私有路径不承诺对外兼容。
- 新生产C文件加入src/sources.cmake相应组。IDF与Host使用相同engine清单；Host的独立executor测试使用相同pixel/OS组，fake资源测试复用resources组。实际TE GPIO同步由IDF使用，Host依旧使用原有shim和专项测试。
- 本次保留文件基名、函数名和linker.lf映射。移动热路径后仍须检查目标ELF的IRAM放置，不能仅根据文件名推断映射生效。
- dev/evidence内历史快照和日志保持原样；它们记录历史路径，不作为维护源码入口。

## 下一批接口整理

core/present_presenter.h暂时保留原名与内容，其engine配置/API、session和control声明尚未拆开。resources/present_memory.h仍会引入engine头，mode/plan也保留原类型依赖。下一批通过最小类型声明、前置声明或专用接口头逐项解除依赖；不与本次文件移动混做。

本次移动和构建验证见[证据](evidence/source-layout-20261106/README.md)。
